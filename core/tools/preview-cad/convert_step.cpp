#include "convert_step.hpp"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Message_ProgressRange.hxx>
#include <OSD_FileSystem.hxx>
#include <Poly_Triangulation.hxx>
#include <RWGltf_CafWriter.hxx>
#include <RWMesh_CoordinateSystem.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_ArrayStreamBuffer.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <vector>

namespace vh::helpers::cad {

namespace {

constexpr uint64_t kDefaultMaxTriangles = 2'000'000;
constexpr uint64_t kDefaultMaxInputBytes = 512ull << 20;
constexpr uint64_t kDefaultMaxEntities = 10'000'000;
constexpr double kAngularDeflection = 0.5;          // radians
constexpr double kCoarseFactor = 4.0;               // the one retry when the triangle cap is hit

// ── In-memory files for RWGltf_CafWriter ────────────────────────────────────────────────────────────────────
// The writer opens its outputs (and a temporary .bin it reads back) through OSD_FileSystem. Serving the
// "vhmem:" protocol from memory keeps the artifact off disk entirely (the sandbox forbids writable opens anyway).

constexpr const char* kMemPrefix = "vhmem:";

class CappedBuffer final : public std::streambuf {
public:
    explicit CappedBuffer(const std::size_t cap) : cap_(cap) {}

    [[nodiscard]] const std::vector<char>& data() const { return data_; }
    [[nodiscard]] bool overflowed() const { return overflowed_; }

protected:
    int_type overflow(const int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        const char c = traits_type::to_char_type(ch);
        return xsputn(&c, 1) == 1 ? ch : traits_type::eof();
    }

    std::streamsize xsputn(const char* s, const std::streamsize n) override {
        if (n <= 0) return 0;
        const auto count = static_cast<std::size_t>(n);
        if (pos_ > cap_ || count > cap_ - pos_) {
            overflowed_ = true;
            return 0;
        }
        if (pos_ + count > data_.size()) data_.resize(pos_ + count);
        std::memcpy(data_.data() + pos_, s, count);
        pos_ += count;
        return n;
    }

    pos_type seekoff(const off_type off, const std::ios_base::seekdir dir, std::ios_base::openmode) override {
        off_type base = 0;
        if (dir == std::ios_base::cur) base = static_cast<off_type>(pos_);
        else if (dir == std::ios_base::end) base = static_cast<off_type>(data_.size());
        const off_type target = base + off;
        if (target < 0) return pos_type(off_type(-1));
        pos_ = static_cast<std::size_t>(target);
        return pos_type(target);
    }

    pos_type seekpos(const pos_type pos, const std::ios_base::openmode which) override {
        return seekoff(off_type(pos), std::ios_base::beg, which);
    }

private:
    std::vector<char> data_;
    std::size_t pos_ = 0;
    std::size_t cap_;
    bool overflowed_ = false;
};

struct OwnedOStream final : std::ostream {
    explicit OwnedOStream(std::shared_ptr<CappedBuffer> buffer) : std::ostream(buffer.get()), owner(std::move(buffer)) {}
    std::shared_ptr<CappedBuffer> owner;
};

struct OwnedIStream final : std::istream {
    explicit OwnedIStream(std::shared_ptr<CappedBuffer> buffer)
        : std::istream(nullptr), owner(std::move(buffer)), view(owner->data().data(), owner->data().size()) {
        rdbuf(&view);
    }
    std::shared_ptr<CappedBuffer> owner;
    Standard_ArrayStreamBuffer view;
};

class MemoryFileSystem final : public OSD_FileSystem {
public:
    void reset(const std::size_t cap) {
        files_.clear();
        cap_ = cap;
    }

    [[nodiscard]] std::shared_ptr<CappedBuffer> file(const std::string& url) const {
        const auto it = files_.find(url);
        return it == files_.end() ? nullptr : it->second;
    }

    [[nodiscard]] bool anyOverflowed() const {
        return std::ranges::any_of(files_, [](const auto& entry) { return entry.second->overflowed(); });
    }

    Standard_Boolean IsSupportedPath(const TCollection_AsciiString& url) const override {
        return url.StartsWith(kMemPrefix);
    }

    Standard_Boolean IsOpenIStream(const std::shared_ptr<std::istream>& stream) const override {
        return stream && !stream->fail();
    }

    Standard_Boolean IsOpenOStream(const std::shared_ptr<std::ostream>& stream) const override {
        return stream && !stream->fail();
    }

    std::shared_ptr<std::istream> OpenIStream(const TCollection_AsciiString& url, const std::ios_base::openmode,
                                              const int64_t offset, const std::shared_ptr<std::istream>&) override {
        const auto buffer = file(url.ToCString());
        if (!buffer) return nullptr;
        auto stream = std::make_shared<OwnedIStream>(buffer);
        if (offset > 0) stream->seekg(offset);
        return stream;
    }

    std::shared_ptr<std::ostream> OpenOStream(const TCollection_AsciiString& url, const std::ios_base::openmode) override {
        auto buffer = std::make_shared<CappedBuffer>(cap_);
        files_[url.ToCString()] = buffer;
        return std::make_shared<OwnedOStream>(buffer);
    }

    std::shared_ptr<std::streambuf> OpenStreamBuffer(const TCollection_AsciiString&, const std::ios_base::openmode,
                                                     const int64_t, int64_t*) override {
        return nullptr;
    }

private:
    std::map<std::string, std::shared_ptr<CappedBuffer>> files_;
    std::size_t cap_ = 0;
};

MemoryFileSystem& memoryFileSystem() {
    static Handle(MemoryFileSystem) fs = new MemoryFileSystem();
    static std::once_flag registered;
    std::call_once(registered, [] { OSD_FileSystem::AddDefaultProtocol(fs, true); });
    return *fs;
}

// ── Helpers ─────────────────────────────────────────────────────────────────────────────────────────────────

void quietOcct() {
    // OCCT reports through Message printers (std::cout by default; runMain already points fd 1 at stderr).
    for (Message_SequenceOfPrinters::Iterator it(Message::DefaultMessenger()->Printers()); it.More(); it.Next())
        it.Value()->SetTraceLevel(Message_Fail);
}

std::string failureText(const Standard_Failure& failure) {
    const char* message = failure.GetMessageString();
    std::string text = failure.DynamicType()->Name();
    if (message && *message) text += std::string(": ") + message;
    return text;
}

void requirePart21(const std::vector<uint8_t>& data) {
    std::size_t i = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) i = 3;
    while (i < data.size() && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n')) ++i;
    constexpr std::string_view magic = "ISO-10303-21";
    if (data.size() - i < magic.size() || std::memcmp(data.data() + i, magic.data(), magic.size()) != 0)
        throw InvalidInput("not a STEP (ISO 10303-21) file");
}

struct MeshStats {
    uint64_t triangles = 0;
    uint64_t nodes = 0;
};

MeshStats meshStats(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    MeshStats stats;
    for (int i = 1; i <= faces.Extent(); ++i) {
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(TopoDS::Face(faces(i)), location);
        if (triangulation.IsNull()) continue;
        stats.triangles += static_cast<uint64_t>(triangulation->NbTriangles());
        stats.nodes += static_cast<uint64_t>(triangulation->NbNodes());
    }
    return stats;
}

MeshStats tessellate(const TopoDS_Shape& shape, const double linear, const double angular) {
    BRepMesh_IncrementalMesh mesher(shape, linear, Standard_False, angular, Standard_False);
    if (!mesher.IsDone()) throw InvalidInput("tessellation failed");
    return meshStats(shape);
}

nlohmann::json convertParsed(STEPCAFControl_Reader& reader, const Args& args, OutputSink& output) {
    const uint64_t maxTriangles = args.u64("max-triangles", kDefaultMaxTriangles);
    const uint64_t maxEntities = args.u64("max-entities", kDefaultMaxEntities);

    const Handle(Interface_InterfaceModel) model = reader.ChangeReader().Model();
    const auto entities = model.IsNull() ? 0 : static_cast<uint64_t>(model->NbEntities());
    if (entities == 0) throw InvalidInput("STEP file has no entities");
    if (entities > maxEntities)
        throw LimitExceeded("STEP file has " + std::to_string(entities) + " entities, limit " + std::to_string(maxEntities));
    const auto roots = static_cast<uint64_t>(std::max(0, reader.NbRootsForTransfer()));
    if (roots == 0) throw InvalidInput("STEP file has no transferable shapes");

    Handle(TDocStd_Document) document;
    XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", document);
    if (!reader.Transfer(document)) throw InvalidInput("STEP transfer produced no shapes");

    const Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence freeShapes;
    shapes->GetFreeShapes(freeShapes);
    if (freeShapes.IsEmpty()) throw InvalidInput("STEP file has no shapes");

    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    for (TDF_LabelSequence::Iterator it(freeShapes); it.More(); it.Next()) {
        const TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(it.Value());
        if (!shape.IsNull()) builder.Add(all, shape);
    }

    Bnd_Box box;
    BRepBndLib::Add(all, box);
    if (box.IsVoid()) throw InvalidInput("STEP file has no geometry");
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double diagonal = std::sqrt(box.SquareExtent());
    if (!std::isfinite(diagonal) || diagonal <= 0.0) throw InvalidInput("STEP geometry has no extent");

    double linear = std::max(diagonal / 1000.0, 1e-9);
    double angular = kAngularDeflection;
    MeshStats stats = tessellate(all, linear, angular);
    if (stats.triangles > maxTriangles) {
        BRepTools::Clean(all);
        linear *= kCoarseFactor;
        angular = std::min(angular * 2.0, 1.0);
        stats = tessellate(all, linear, angular);
        if (stats.triangles > maxTriangles)
            throw LimitExceeded("tessellation needs " + std::to_string(stats.triangles) + " triangles, limit " +
                                std::to_string(maxTriangles));
    }
    if (stats.triangles == 0) throw InvalidInput("tessellation produced no triangles");

    auto& fs = memoryFileSystem();
    fs.reset(static_cast<std::size_t>(std::min<uint64_t>(output.limit(), SIZE_MAX / 2)));
    const std::string glbUrl = std::string(kMemPrefix) + "/model.glb";
    RWGltf_CafWriter writer(glbUrl.c_str(), Standard_True);
    writer.SetMergeFaces(true);
    writer.ChangeCoordinateSystemConverter().SetInputLengthUnit(0.001);   // OCCT session unit: millimetres
    writer.ChangeCoordinateSystemConverter().SetInputCoordinateSystem(RWMesh_CoordinateSystem_Zup);
    const TColStd_IndexedDataMapOfStringString fileInfo;
    const bool written = writer.Perform(document, fileInfo, Message_ProgressRange());
    if (fs.anyOverflowed()) throw LimitExceeded("GLB exceeds " + std::to_string(output.limit()) + " bytes");
    const auto glb = fs.file(glbUrl);
    if (!written || !glb || glb->data().size() < 12 || std::memcmp(glb->data().data(), "glTF", 4) != 0)
        throw std::runtime_error("GLB writer failed");

    output.write(std::span(reinterpret_cast<const uint8_t*>(glb->data().data()), glb->data().size()));
    fs.reset(0);

    return {
        {"ok", true},
        {"format", "glb"},
        {"triangles", stats.triangles},
        {"nodes", stats.nodes},
        {"bbox", {xmin, ymin, zmin, xmax, ymax, zmax}},
        {"bbox_units", "mm"},
        {"entities", entities},
        {"roots", roots},
        {"linear_deflection", linear},
        {"angular_deflection", angular},
    };
}

}

nlohmann::json convertStep(const Args& args, RangeClient& input, OutputSink& output) {
    quietOcct();
    const std::vector<uint8_t> data = input.readAll(args.u64("max-input-bytes", kDefaultMaxInputBytes));
    if (data.empty()) throw InvalidInput("empty input");
    requirePart21(data);

    try {
        Standard_ArrayStreamBuffer buffer(reinterpret_cast<const char*>(data.data()), data.size());
        std::istream stream(&buffer);
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(false);
        reader.SetPropsMode(false);
        reader.SetGDTMode(false);
        reader.SetMatMode(true);
        reader.SetViewMode(false);
        if (const auto status = reader.ChangeReader().ReadStream("input.step", stream); status != IFSelect_RetDone)
            throw InvalidInput("STEP parse failed (status " + std::to_string(static_cast<int>(status)) + ")");
        return convertParsed(reader, args, output);
    } catch (const Standard_Failure& failure) {
        throw InvalidInput("OCCT: " + failureText(failure));
    }
}

nlohmann::json selftestSandbox(const Args& args) {
    const auto attempt = [](const bool succeeded, const int err) {
        return nlohmann::json{{"blocked", !succeeded}, {"errno", succeeded ? 0 : err},
                              {"error", succeeded ? "" : std::strerror(err)}};
    };

    nlohmann::json report = nlohmann::json::object();

    const std::string writePath = args.str("write-path", "/tmp/vaulthalla-preview-cad-selftest");
    {
        const int fd = ::open(writePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        report["open_write"] = attempt(fd >= 0, err);
    }
    if (const auto readPath = args.str("read-path", ""); !readPath.empty()) {
        const int fd = ::open(readPath.c_str(), O_RDONLY | O_CLOEXEC);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        report["open_read_outside"] = attempt(fd >= 0, err);
    }
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        report["socket_inet"] = attempt(fd >= 0, err);
    }
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        report["socket_unix"] = attempt(fd >= 0, err);
    }
    {
        const pid_t pid = ::fork();
        const int err = errno;
        if (pid == 0) ::_exit(0);
        if (pid > 0) ::waitpid(pid, nullptr, 0);
        report["fork"] = attempt(pid >= 0, err);
    }
    {
        char* const argv[] = {const_cast<char*>("/bin/true"), nullptr};
        char* const envp[] = {nullptr};
        ::execve("/bin/true", argv, envp);   // returns only on failure
        report["execve"] = attempt(false, errno);
    }
    {
        const int rc = ::kill(::getppid(), 0);
        report["signal_parent"] = attempt(rc == 0, errno);
    }
    {
        const int fd = ::open("/usr/lib/os-release", O_RDONLY | O_CLOEXEC);
        const int err = errno;
        if (fd >= 0) ::close(fd);
        report["open_read_usr"] = attempt(fd >= 0, err);
    }

    return {{"ok", true}, {"selftest", report}};
}

}
