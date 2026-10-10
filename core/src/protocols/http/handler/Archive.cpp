#include "protocols/http/handler/Archive.hpp"

#include "fs/cache/Registry.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "rbac/Actor.hpp"
#include "runtime/Deps.hpp"
#include "share/Principal.hpp"
#include "share/Scope.hpp"
#include "share/TargetResolver.hpp"
#include "storage/PlaintextReader.hpp"

#include <sodium.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace vh::protocols::http::handler::archive {

namespace {

// --- Naming -------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string relativeArchivePath(const std::string& rootVaultPath, const std::string& entryVaultPath) {
    const auto root = share::Scope::normalizeVaultPath(rootVaultPath);
    const auto entry = share::Scope::normalizeVaultPath(entryVaultPath);
    if (!share::Scope::contains(root, entry) || entry == root)
        throw std::runtime_error("Archive entry escapes the selected directory");
    std::string rel = root == "/" ? entry.substr(1) : entry.substr(root.size());
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    return rel;
}

// One path component as valid UTF-8 without separators or control bytes: anything else becomes '_'.
[[nodiscard]] std::string sanitizeComponent(const std::string_view in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        const auto c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            out.push_back(c < 0x20 || c == 0x7f || c == '\\' ? '_' : static_cast<char>(c));
            ++i;
            continue;
        }
        const std::size_t len = (c & 0xE0u) == 0xC0u ? 2 : (c & 0xF0u) == 0xE0u ? 3 : (c & 0xF8u) == 0xF0u ? 4 : 0;
        bool ok = len != 0 && i + len <= in.size();
        uint32_t cp = len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (std::size_t k = 1; ok && k < len; ++k) {
            const auto b = static_cast<unsigned char>(in[i + k]);
            ok = (b & 0xC0u) == 0x80u;
            cp = (cp << 6u) | (b & 0x3Fu);
        }
        if (ok) {
            const uint32_t min = len == 2 ? 0x80u : len == 3 ? 0x800u : 0x10000u;
            ok = cp >= min && cp <= 0x10FFFFu && (cp < 0xD800u || cp > 0xDFFFu);
        }
        if (!ok) {
            out.push_back('_');
            ++i;
            continue;
        }
        out.append(in.substr(i, len));
        i += len;
    }
    return out;
}

// In-flight HTTP upload staging (`.upload-http-<id>-<file>.part`, see upload/Coordinator.cpp) is not content.
[[nodiscard]] bool isUploadStaging(const std::string_view name) {
    return name.starts_with(".upload-http-") && name.ends_with(".part");
}

// --- Planning -----------------------------------------------------------------------------------------------------

void sortByName(std::vector<std::shared_ptr<fs::model::Entry>>& entries) {
    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        if (!a || !b) return static_cast<bool>(b);
        return a->name < b->name;
    });
}

void push(std::vector<Member>& members, const std::string& rel, const std::shared_ptr<fs::model::Entry>& entry,
          std::shared_ptr<fs::model::File> file) {
    if (members.size() >= kMaxEntries)
        throw std::length_error("This folder has more than " + std::to_string(kMaxEntries) +
                                " entries; download it in parts");
    Member member;
    member.directory = !file;
    member.name = safeName(rel, member.directory);
    member.size = file ? static_cast<uint64_t>(file->size_bytes) : 0;
    member.mtime = entry->updated_at;
    member.file = std::move(file);
    members.push_back(std::move(member));
}

void planHuman(std::vector<Member>& members, const access::Caller& caller, const access::Target& root,
               const std::shared_ptr<fs::model::Entry>& directory) {
    auto children = runtime::Deps::get().fsCache->listDir(directory->id, false);
    sortByName(children);
    for (const auto& child : children) {
        if (child && !child->isDirectory() && isUploadStaging(child->name)) continue;
        access::requireHumanChild(caller, root, child);
        const auto rel = relativeArchivePath(root.entry->path.string(), child->path.string());
        if (child->isDirectory()) {
            push(members, rel, child, nullptr);
            planHuman(members, caller, root, child);
        } else if (auto file = std::dynamic_pointer_cast<fs::model::File>(child)) {
            push(members, rel, child, std::move(file));
        }
        // Symlinks are never served over HTTP (see access::checkExpect): left out of archives too.
    }
}

void planShare(std::vector<Member>& members, const access::Target& root,
               const share::ResolvedTarget& directoryTarget) {
    const auto& share = *root.share;
    if (!directoryTarget.entry || directoryTarget.target_type != share::TargetType::Directory)
        throw std::runtime_error("Archive target is not a directory");
    const auto actor = rbac::Actor::share(share.principal);

    share::ResolvedTarget listTarget;
    std::vector<std::shared_ptr<fs::model::Entry>> children;
    try {
        listTarget = share.resolver->resolve(actor, {
            .path = directoryTarget.share_path,
            .operation = share::Operation::List,
            .path_mode = share::TargetPathMode::ShareRelative,
            .expected_target_type = share::TargetType::Directory
        });
        children = share.resolver->listChildren(actor, listTarget);
    } catch (const std::exception& e) {
        throw access::Forbidden(e.what());
    }
    sortByName(children);

    for (const auto& child : children) {
        if (!child) throw access::NotFound("Archive child is unavailable");
        const bool directory = child->isDirectory();
        auto file = directory ? nullptr : std::dynamic_pointer_cast<fs::model::File>(child);
        if (!directory && (!file || isUploadStaging(child->name))) continue;  // symlinks, upload staging

        // The same scope + share RBAC decision as resolve(…, Download), without two DB lookups per entry.
        share::ResolvedTarget childTarget;
        try {
            childTarget = share.resolver->resolveListedChild(*share.principal, listTarget, child,
                                                             share::Operation::Download);
        } catch (const std::exception& e) {
            throw access::Forbidden(e.what());
        }
        const auto rel = relativeArchivePath(root.vaultPath, child->path.string());
        if (directory) {
            push(members, rel, child, nullptr);
            planShare(members, root, childTarget);
        } else {
            push(members, rel, child, std::move(file));
        }
    }
}

// --- ZIP records --------------------------------------------------------------------------------------------------

constexpr uint32_t kMax32 = 0xFFFFFFFFu;
constexpr uint16_t kMax16 = 0xFFFFu;
constexpr uint16_t kFlagDescriptor = 0x0008u;  // bit 3: CRC and sizes follow the data
constexpr uint16_t kFlagUtf8 = 0x0800u;        // bit 11: names are UTF-8
constexpr uint16_t kMadeBy = (3u << 8u) | 45u; // Unix, spec 4.5 (ZIP64)
constexpr uint16_t kVersionPlain = 20;
constexpr uint16_t kVersionZip64 = 45;
constexpr uint64_t kLocalFixed = 30, kCentralFixed = 46, kEndFixed = 22, kZip64End = 56, kZip64Locator = 20;
constexpr uint64_t kTimeExtra = 9;             // 0x5455 extended timestamp: flags + UTC mtime
constexpr uint64_t kLocalZip64Extra = 20;      // 0x0001 with both sizes (zero: they follow in the descriptor)
constexpr std::size_t kCentralBatchBytes = 64u * 1024u;

[[nodiscard]] bool zip64Sized(const Member& m) { return m.size >= kMax32; }
[[nodiscard]] bool hasData(const Member& m) { return !m.directory && m.size > 0; }

[[nodiscard]] uint64_t localHeaderSize(const Member& m) {
    return kLocalFixed + m.name.size() + kTimeExtra + (zip64Sized(m) ? kLocalZip64Extra : 0);
}

[[nodiscard]] uint64_t descriptorSize(const Member& m) {
    if (!hasData(m)) return 0;
    return zip64Sized(m) ? 24 : 16;
}

[[nodiscard]] uint64_t centralZip64Payload(const Member& m, const uint64_t offset) {
    return (zip64Sized(m) ? 16u : 0u) + (offset >= kMax32 ? 8u : 0u);
}

[[nodiscard]] uint64_t centralRecordSize(const Member& m, const uint64_t offset) {
    const auto payload = centralZip64Payload(m, offset);
    return kCentralFixed + m.name.size() + kTimeExtra + (payload ? 4 + payload : 0);
}

struct Layout {
    std::vector<uint64_t> offsets;  // local header offset per member
    uint64_t centralOffset{};
    uint64_t centralSize{};
    uint64_t total{};
    bool zip64End{false};
};

[[nodiscard]] Layout layoutOf(const std::vector<Member>& members) {
    Layout layout;
    layout.offsets.reserve(members.size());
    uint64_t pos = 0;
    for (const auto& m : members) {
        if (m.name.empty() || m.name.size() > kMax16) throw std::length_error("Archive entry name is too long");
        if (m.directory && m.size != 0) throw std::invalid_argument("Archive directory entries carry no data");
        layout.offsets.push_back(pos);
        pos += localHeaderSize(m) + m.size + descriptorSize(m);
    }
    layout.centralOffset = pos;
    for (std::size_t i = 0; i < members.size(); ++i) pos += centralRecordSize(members[i], layout.offsets[i]);
    layout.centralSize = pos - layout.centralOffset;
    layout.zip64End = members.size() >= kMax16 || layout.centralOffset >= kMax32 || layout.centralSize >= kMax32;
    if (layout.zip64End) pos += kZip64End + kZip64Locator;
    layout.total = pos + kEndFixed;
    return layout;
}

class Bytes {
public:
    explicit Bytes(std::vector<uint8_t>& out) : out_(out) {}
    void u8(const uint8_t v) { out_.push_back(v); }
    void u16(const uint16_t v) { for (int s = 0; s < 16; s += 8) out_.push_back(static_cast<uint8_t>(v >> s)); }
    void u32(const uint32_t v) { for (int s = 0; s < 32; s += 8) out_.push_back(static_cast<uint8_t>(v >> s)); }
    void u64(const uint64_t v) { for (int s = 0; s < 64; s += 8) out_.push_back(static_cast<uint8_t>(v >> s)); }
    void str(const std::string& v) { out_.insert(out_.end(), v.begin(), v.end()); }

private:
    std::vector<uint8_t>& out_;
};

struct DosTime {
    uint16_t time{};
    uint16_t date{};
};

[[nodiscard]] DosTime dosTime(const std::time_t t) {
    std::tm tm{};
    if (!localtime_r(&t, &tm) || tm.tm_year < 80) return {0, static_cast<uint16_t>((1u << 5u) | 1u)};  // 1980-01-01
    if (tm.tm_year > 207) return {static_cast<uint16_t>((23u << 11u) | (59u << 5u) | 29u),
                                  static_cast<uint16_t>((127u << 9u) | (12u << 5u) | 31u)};
    return {static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (std::min(tm.tm_sec, 59) / 2)),
            static_cast<uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday)};
}

[[nodiscard]] uint32_t unixTime(const std::time_t t) {
    return static_cast<uint32_t>(std::clamp<std::time_t>(t, 0, std::numeric_limits<int32_t>::max()));
}

[[nodiscard]] uint16_t flagsOf(const Member& m) { return kFlagUtf8 | (hasData(m) ? kFlagDescriptor : 0); }

void timeExtra(Bytes& b, const Member& m) {
    b.u16(0x5455);
    b.u16(5);
    b.u8(1);  // mtime present
    b.u32(unixTime(m.mtime));
}

void appendLocal(std::vector<uint8_t>& out, const Member& m) {
    Bytes b(out);
    const auto when = dosTime(m.mtime);
    const bool z64 = zip64Sized(m);
    b.u32(0x04034b50u);
    b.u16(z64 ? kVersionZip64 : kVersionPlain);
    b.u16(flagsOf(m));
    b.u16(0);  // STORE
    b.u16(when.time);
    b.u16(when.date);
    b.u32(0);  // CRC: in the data descriptor (or 0 for an entry without data)
    b.u32(z64 ? kMax32 : 0);
    b.u32(z64 ? kMax32 : 0);
    b.u16(static_cast<uint16_t>(m.name.size()));
    b.u16(static_cast<uint16_t>(kTimeExtra + (z64 ? kLocalZip64Extra : 0)));
    b.str(m.name);
    timeExtra(b, m);
    if (z64) {
        b.u16(0x0001);
        b.u16(16);
        b.u64(0);
        b.u64(0);
    }
}

void appendDescriptor(std::vector<uint8_t>& out, const Member& m, const uint32_t crc) {
    Bytes b(out);
    b.u32(0x08074b50u);
    b.u32(crc);
    if (zip64Sized(m)) {
        b.u64(m.size);
        b.u64(m.size);
    } else {
        b.u32(static_cast<uint32_t>(m.size));
        b.u32(static_cast<uint32_t>(m.size));
    }
}

void appendCentral(std::vector<uint8_t>& out, const Member& m, const uint32_t crc, const uint64_t offset) {
    Bytes b(out);
    const auto when = dosTime(m.mtime);
    const bool z64Size = zip64Sized(m);
    const bool z64Offset = offset >= kMax32;
    const auto payload = centralZip64Payload(m, offset);
    b.u32(0x02014b50u);
    b.u16(kMadeBy);
    b.u16(payload ? kVersionZip64 : kVersionPlain);
    b.u16(flagsOf(m));
    b.u16(0);
    b.u16(when.time);
    b.u16(when.date);
    b.u32(crc);
    b.u32(z64Size ? kMax32 : static_cast<uint32_t>(m.size));
    b.u32(z64Size ? kMax32 : static_cast<uint32_t>(m.size));
    b.u16(static_cast<uint16_t>(m.name.size()));
    b.u16(static_cast<uint16_t>(kTimeExtra + (payload ? 4 + payload : 0)));
    b.u16(0);  // comment
    b.u16(0);  // disk
    b.u16(0);  // internal attributes
    b.u32(m.directory ? ((040755u << 16u) | 0x10u) : (0100644u << 16u));
    b.u32(z64Offset ? kMax32 : static_cast<uint32_t>(offset));
    b.str(m.name);
    timeExtra(b, m);
    if (payload) {
        b.u16(0x0001);
        b.u16(static_cast<uint16_t>(payload));
        if (z64Size) {
            b.u64(m.size);
            b.u64(m.size);
        }
        if (z64Offset) b.u64(offset);
    }
}

void appendEnd(std::vector<uint8_t>& out, const Layout& layout, const uint64_t entries) {
    Bytes b(out);
    if (layout.zip64End) {
        const auto zip64EndOffset = layout.centralOffset + layout.centralSize;
        b.u32(0x06064b50u);
        b.u64(kZip64End - 12);
        b.u16(kMadeBy);
        b.u16(kVersionZip64);
        b.u32(0);
        b.u32(0);
        b.u64(entries);
        b.u64(entries);
        b.u64(layout.centralSize);
        b.u64(layout.centralOffset);
        b.u32(0x07064b50u);
        b.u32(0);
        b.u64(zip64EndOffset);
        b.u32(1);
    }
    const auto count = static_cast<uint16_t>(std::min<uint64_t>(entries, kMax16));
    b.u32(0x06054b50u);
    b.u16(0);
    b.u16(0);
    b.u16(count);
    b.u16(count);
    b.u32(static_cast<uint32_t>(std::min<uint64_t>(layout.centralSize, kMax32)));
    b.u32(static_cast<uint32_t>(std::min<uint64_t>(layout.centralOffset, kMax32)));
    b.u16(0);
}

// --- Streaming ----------------------------------------------------------------------------------------------------

class ZipStream final : public storage::PlaintextReader {
public:
    ZipStream(std::vector<Member> members, Opener open)
        : members_(std::move(members)), open_(std::move(open)), layout_(layoutOf(members_)),
          crcs_(members_.size(), 0) {
        generation_.size = layout_.total;
    }

    ~ZipStream() override { wipePending(); }

    [[nodiscard]] uint64_t size() const override { return layout_.total; }
    [[nodiscard]] const storage::Generation& generation() const override { return generation_; }

    std::size_t read(const uint64_t offset, const std::span<uint8_t> out) override {
        if (offset != cursor_) throw std::logic_error("Archive streams are read sequentially");
        std::size_t done = 0;
        while (done < out.size()) {
            if (pendingPos_ < pending_.size()) {
                const auto n = std::min(out.size() - done, pending_.size() - pendingPos_);
                std::memcpy(out.data() + done, pending_.data() + pendingPos_, n);
                pendingPos_ += n;
                done += n;
            } else if (reader_) {
                done += pump(out.subspan(done));
            } else if (!refill(cursor_ + done)) {
                break;
            }
        }
        cursor_ += done;
        return done;
    }

private:
    enum class Stage { Members, Central, End, Done };

    void wipePending() {
        if (!pending_.empty()) sodium_memzero(pending_.data(), pending_.size());
        pending_.clear();
        pendingPos_ = 0;
    }

    static void expectAt(const uint64_t position, const uint64_t planned) {
        if (position != planned) throw std::logic_error("Archive stream diverged from its planned layout");
    }

    // Queues the next records. False once the archive is complete.
    bool refill(const uint64_t position) {
        wipePending();
        switch (stage_) {
            case Stage::Members:
                if (index_ == members_.size()) {
                    expectAt(position, layout_.centralOffset);
                    stage_ = Stage::Central;
                    index_ = 0;
                    return true;
                }
                expectAt(position, layout_.offsets[index_]);
                startMember();
                return true;
            case Stage::Central:
                if (index_ == members_.size()) {
                    expectAt(position, layout_.centralOffset + layout_.centralSize);
                    stage_ = Stage::End;
                    return true;
                }
                while (index_ < members_.size() && pending_.size() < kCentralBatchBytes) {
                    appendCentral(pending_, members_[index_], crcs_[index_], layout_.offsets[index_]);
                    ++index_;
                }
                return true;
            case Stage::End:
                appendEnd(pending_, layout_, members_.size());
                stage_ = Stage::Done;
                return true;
            case Stage::Done:
                return false;
        }
        return false;
    }

    void startMember() {
        const auto& m = members_[index_];
        appendLocal(pending_, m);
        if (!hasData(m)) {
            members_[index_].file.reset();
            ++index_;
            return;
        }

        auto reader = open_ ? open_(m) : nullptr;
        if (!reader) throw std::runtime_error("Archive member is unavailable");
        if (reader->size() != m.size)
            throw storage::IntegrityError("Archive member " + m.name + " changed since the download started");
        crc_ = static_cast<uint32_t>(crc32_z(0L, nullptr, 0));
        memberDone_ = 0;

        if (m.size <= kSmallMemberBytes) {
            // One authenticated pass: the bytes are known good before any of them is queued.
            auto bytes = reader->readAllAuthenticated(kSmallMemberBytes);
            if (bytes.size() != m.size) {
                sodium_memzero(bytes.data(), bytes.size());
                throw storage::IntegrityError("Archive member " + m.name + " is shorter than recorded");
            }
            crc_ = static_cast<uint32_t>(crc32_z(crc_, bytes.data(), bytes.size()));
            pending_.insert(pending_.end(), bytes.begin(), bytes.end());
            sodium_memzero(bytes.data(), bytes.size());
            finishMember();
            return;
        }
        reader_ = std::move(reader);
    }

    std::size_t pump(const std::span<uint8_t> out) {
        const auto& m = members_[index_];
        const auto want = static_cast<std::size_t>(std::min<uint64_t>(out.size(), m.size - memberDone_));
        const auto n = reader_->read(memberDone_, out.first(want));
        if (n == 0) throw storage::IntegrityError("Archive member " + m.name + " ended before its recorded size");
        crc_ = static_cast<uint32_t>(crc32_z(crc_, out.data(), n));
        memberDone_ += n;
        if (memberDone_ == m.size) {
            // The descriptor's CRC vouches for these bytes: only write it once the whole member authenticated.
            reader_->requireAuthenticated();
            reader_.reset();
            finishMember();
        }
        return n;
    }

    void finishMember() {
        members_[index_].file.reset();  // the plan shrinks as the stream advances
        crcs_[index_] = crc_;
        appendDescriptor(pending_, members_[index_], crc_);
        ++index_;
    }

    std::vector<Member> members_;
    Opener open_;
    Layout layout_;
    std::vector<uint32_t> crcs_;
    storage::Generation generation_;

    Stage stage_{Stage::Members};
    std::size_t index_{0};
    uint64_t cursor_{0};
    std::vector<uint8_t> pending_;
    std::size_t pendingPos_{0};
    std::shared_ptr<storage::PlaintextReader> reader_;
    uint64_t memberDone_{0};
    uint32_t crc_{0};
};

}

std::string safeName(const std::string_view relativePath, const bool directory) {
    std::string out;
    std::size_t start = 0;
    while (start <= relativePath.size()) {
        const auto slash = relativePath.find('/', start);
        const auto component = relativePath.substr(start, slash == std::string_view::npos ? std::string_view::npos
                                                                                           : slash - start);
        if (!component.empty()) {
            if (component == "." || component == "..")
                throw std::invalid_argument("Archive entry path is unsafe");
            if (!out.empty()) out.push_back('/');
            out += sanitizeComponent(component);
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    if (out.empty()) throw std::invalid_argument("Archive entry path is empty");
    if (directory) out.push_back('/');
    return out;
}

std::vector<Member> plan(const access::Caller& caller, const access::Target& root) {
    if (!root.entry || !root.entry->isDirectory()) throw std::invalid_argument("Archive target is not a directory");
    std::vector<Member> members;
    if (root.share) {
        if (!root.share->resolved) throw std::runtime_error("Share archive target is unresolved");
        planShare(members, root, *root.share->resolved);
    } else {
        planHuman(members, caller, root, root.entry);
    }
    return members;
}

uint64_t archiveSize(const std::vector<Member>& members) { return layoutOf(members).total; }

std::shared_ptr<storage::PlaintextReader> stream(std::vector<Member> members, Opener open) {
    return std::make_shared<ZipStream>(std::move(members), std::move(open));
}

}
