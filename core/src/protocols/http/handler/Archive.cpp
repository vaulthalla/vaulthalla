#include "protocols/http/handler/Archive.hpp"

#include "fs/cache/Registry.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "rbac/Actor.hpp"
#include "runtime/Deps.hpp"
#include "share/Principal.hpp"
#include "share/Scope.hpp"
#include "share/TargetResolver.hpp"
#include "storage/Engine.hpp"
#include "storage/PlaintextReader.hpp"

#include <zlib.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vh::protocols::http::handler::archive {

namespace {

constexpr uint64_t kMaxArchiveSourceBytes = 256ull * 1024ull * 1024ull;
constexpr uint64_t kMaxArchiveBytes = 320ull * 1024ull * 1024ull;
constexpr uint32_t kMaxArchiveEntries = 4096;
constexpr uint16_t kZipDateJanOne1980 = 33;

[[nodiscard]] std::string normalizedVaultPath(const std::string& value) {
    return vh::share::Scope::normalizeVaultPath(value);
}

[[nodiscard]] std::string relativeArchivePath(const std::string& rootVaultPath, const std::string& entryVaultPath) {
    const auto root = normalizedVaultPath(rootVaultPath);
    const auto entry = normalizedVaultPath(entryVaultPath);
    if (!vh::share::Scope::contains(root, entry))
        throw std::runtime_error("Archive entry escapes selected directory");
    if (entry == root) return {};

    std::string rel = root == "/" ? entry.substr(1) : entry.substr(root.size());
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    if (rel.empty()) throw std::runtime_error("Archive entry has empty relative path");
    return rel;
}

[[nodiscard]] std::string safeArchivePath(std::string value, const bool directory) {
    std::ranges::replace(value, '\\', '/');
    std::stringstream stream(value);
    std::string component;
    std::vector<std::string> parts;

    while (std::getline(stream, component, '/')) {
        if (component.empty() || component == ".") continue;
        if (component == ".." || component.find('\0') != std::string::npos)
            throw std::runtime_error("Archive entry path is unsafe");
        parts.push_back(component);
    }

    if (parts.empty()) throw std::runtime_error("Archive entry path is empty");
    std::string out;
    for (const auto& part : parts) {
        if (!out.empty()) out.push_back('/');
        out += part;
    }
    if (directory && !out.ends_with('/')) out.push_back('/');
    return out;
}

void appendU16(std::vector<uint8_t>& out, const uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<uint8_t>& out, const uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void appendBytes(std::vector<uint8_t>& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
}

void appendBytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& value) {
    out.insert(out.end(), value.begin(), value.end());
}

struct ZipEntryRecord {
    std::string name;
    uint32_t crc{};
    uint32_t size{};
    uint32_t localHeaderOffset{};
    bool directory{};
};

class ZipBuilder {
public:
    void addDirectory(const std::string& archivePath) {
        appendEntry(safeArchivePath(archivePath, true), {}, true);
    }

    void addFile(const std::string& archivePath, const std::vector<uint8_t>& data) {
        appendEntry(safeArchivePath(archivePath, false), data, false);
    }

    [[nodiscard]] std::vector<uint8_t> finish() && {
        if (data_.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Archive exceeds ZIP32 size limit");

        const auto centralDirectoryOffset = static_cast<uint32_t>(data_.size());
        for (const auto& record : records_) appendCentralDirectoryRecord(record);

        if (data_.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Archive exceeds ZIP32 size limit");
        const auto centralDirectorySize = static_cast<uint32_t>(data_.size() - centralDirectoryOffset);

        appendU32(data_, 0x06054b50u);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, static_cast<uint16_t>(records_.size()));
        appendU16(data_, static_cast<uint16_t>(records_.size()));
        appendU32(data_, centralDirectorySize);
        appendU32(data_, centralDirectoryOffset);
        appendU16(data_, 0);

        ensureArchiveSize();
        return std::move(data_);
    }

private:
    void appendEntry(const std::string& name, const std::vector<uint8_t>& data, const bool directory) {
        if (records_.size() >= kMaxArchiveEntries)
            throw std::runtime_error("Archive entry count exceeds limit");
        if (name.size() > std::numeric_limits<uint16_t>::max())
            throw std::runtime_error("Archive entry name is too long");
        if (data.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Archive entry exceeds ZIP32 size limit");
        if (data_.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Archive exceeds ZIP32 size limit");

        const auto crc = static_cast<uint32_t>(crc32(0L, data.data(), static_cast<uInt>(data.size())));
        const auto size = static_cast<uint32_t>(data.size());
        const auto localOffset = static_cast<uint32_t>(data_.size());

        appendU32(data_, 0x04034b50u);
        appendU16(data_, 20);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, kZipDateJanOne1980);
        appendU32(data_, crc);
        appendU32(data_, size);
        appendU32(data_, size);
        appendU16(data_, static_cast<uint16_t>(name.size()));
        appendU16(data_, 0);
        appendBytes(data_, name);
        appendBytes(data_, data);
        records_.push_back({
            .name = name,
            .crc = crc,
            .size = size,
            .localHeaderOffset = localOffset,
            .directory = directory
        });
        ensureArchiveSize();
    }

    void appendCentralDirectoryRecord(const ZipEntryRecord& record) {
        appendU32(data_, 0x02014b50u);
        appendU16(data_, 20);
        appendU16(data_, 20);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, kZipDateJanOne1980);
        appendU32(data_, record.crc);
        appendU32(data_, record.size);
        appendU32(data_, record.size);
        appendU16(data_, static_cast<uint16_t>(record.name.size()));
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU16(data_, 0);
        appendU32(data_, record.directory ? 0x00100000u : 0);
        appendU32(data_, record.localHeaderOffset);
        appendBytes(data_, record.name);
    }

    void ensureArchiveSize() const {
        if (data_.size() > kMaxArchiveBytes)
            throw std::runtime_error("Archive output exceeds limit");
    }

    std::vector<uint8_t> data_;
    std::vector<ZipEntryRecord> records_;
};


void addFile(ZipBuilder& zip, uint64_t& sourceBytes, const std::shared_ptr<vh::storage::Engine>& engine,
             const std::shared_ptr<vh::fs::model::File>& file, const std::string& archivePath) {
    if (!file) throw std::runtime_error("Archive file is unavailable");
    if (sourceBytes + file->size_bytes > kMaxArchiveSourceBytes)
        throw std::length_error("Archive source bytes exceed limit");
    std::vector<uint8_t> bytes;
    if (file->size_bytes > 0) {
        const auto reader = engine->openPlaintextReader(file);
        bytes = vh::storage::readAll(*reader, kMaxArchiveSourceBytes - sourceBytes);
    }
    sourceBytes += bytes.size();
    zip.addFile(archivePath, bytes);
    std::fill(bytes.begin(), bytes.end(), uint8_t{0});
}

void addHumanDirectory(ZipBuilder& zip, uint64_t& sourceBytes, const access::Caller& caller,
                       const access::Target& root, const std::shared_ptr<vh::fs::model::Entry>& directory) {
    access::requireHumanChild(caller, root, directory);
    for (const auto& child : vh::runtime::Deps::get().fsCache->listDir(directory->id, false)) {
        access::requireHumanChild(caller, root, child);
        const auto rel = relativeArchivePath(root.entry->path.string(), child->path.string());
        if (child->isDirectory()) {
            zip.addDirectory(rel);
            addHumanDirectory(zip, sourceBytes, caller, root, child);
        } else {
            addFile(zip, sourceBytes, root.engine, std::dynamic_pointer_cast<vh::fs::model::File>(child), rel);
        }
    }
}

void addShareDirectory(ZipBuilder& zip, uint64_t& sourceBytes, const access::Target& root,
                       const vh::share::ResolvedTarget& directoryTarget) {
    const auto& share = *root.share;
    if (!directoryTarget.entry || directoryTarget.target_type != vh::share::TargetType::Directory)
        throw std::runtime_error("Archive target is not a directory");
    const auto actor = vh::rbac::Actor::share(share.principal);
    const auto listTarget = share.resolver->resolve(actor, {
        .path = directoryTarget.share_path,
        .operation = vh::share::Operation::List,
        .path_mode = vh::share::TargetPathMode::ShareRelative,
        .expected_target_type = vh::share::TargetType::Directory
    });
    for (const auto& child : share.resolver->listChildren(actor, listTarget)) {
        if (!child) throw std::runtime_error("Archive child is unavailable");
        const auto sharePath = vh::share::TargetResolver::shareRelativePath(*share.principal, child->path.string());
        const auto rel = relativeArchivePath(root.vaultPath, child->path.string());
        const auto childTarget = share.resolver->resolve(actor, {
            .path = sharePath,
            .operation = vh::share::Operation::Download,
            .path_mode = vh::share::TargetPathMode::ShareRelative,
            .expected_target_type = child->isDirectory() ? vh::share::TargetType::Directory : vh::share::TargetType::File
        });
        if (child->isDirectory()) {
            zip.addDirectory(rel);
            addShareDirectory(zip, sourceBytes, root, childTarget);
        } else {
            addFile(zip, sourceBytes, root.engine, std::dynamic_pointer_cast<vh::fs::model::File>(childTarget.entry), rel);
        }
    }
}

}

std::vector<uint8_t> build(const access::Caller& caller, const access::Target& root) {
    if (!root.entry || !root.entry->isDirectory()) throw std::invalid_argument("Archive target is not a directory");
    ZipBuilder zip;
    uint64_t sourceBytes = 0;
    if (root.share) {
        if (!root.share->resolved) throw std::runtime_error("Share archive target is unresolved");
        addShareDirectory(zip, sourceBytes, root, *root.share->resolved);
    } else {
        addHumanDirectory(zip, sourceBytes, caller, root, root.entry);
    }
    return std::move(zip).finish();
}

}
