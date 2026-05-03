#include "protocols/http/Router.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "config/Registry.hpp"
#include "auth/Manager.hpp"
#include "protocols/http/handler/preview/Image.hpp"
#include "protocols/http/handler/preview/Pdf.hpp"
#include "fs/ops/file.hpp"
#include "runtime/Deps.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "fs/cache/Registry.hpp"
#include "stats/model/CacheStats.hpp"
#include "protocols/http/model/preview/Request.hpp"
#include "log/Registry.hpp"
#include "protocols/cookie.hpp"
#include "protocols/ws/Session.hpp"
#include "auth/session/Manager.hpp"
#include "share/Manager.hpp"
#include "share/TargetResolver.hpp"
#include "share/Scope.hpp"
#include "identities/User.hpp"
#include "storage/CloudEngine.hpp"
#include "rbac/resolver/Vault.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <nlohmann/json.hpp>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using namespace vh::stats::model;
using namespace vh::fs::model;
using namespace vh::fs::ops;

static std::unordered_map<std::string, std::string> parse_query_params(const std::string& target);
static bool isShareModeRequestTarget(const std::string& target);

namespace {
[[nodiscard]] std::shared_ptr<vh::protocols::ws::Session> defaultPreviewSessionResolver(
    const vh::protocols::http::request& req
) {
    if (isShareModeRequestTarget(std::string(req.target()))) {
        const auto shareRefresh = vh::protocols::extractCookie(req, "share_refresh");
        if (shareRefresh.empty()) throw std::runtime_error("Share refresh token not set");
        return vh::runtime::Deps::get().sessionManager->validateRawShareRefreshToken(shareRefresh);
    }

    const auto refresh = vh::protocols::extractCookie(req, "refresh");
    if (refresh.empty()) throw std::runtime_error("Refresh token not set");
    return vh::runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh);
}

[[nodiscard]] std::shared_ptr<vh::share::Manager> defaultSharePreviewManager() {
    return std::make_shared<vh::share::Manager>();
}

[[nodiscard]] std::shared_ptr<vh::share::TargetResolver> defaultSharePreviewResolver() {
    return std::make_shared<vh::share::TargetResolver>();
}

[[nodiscard]] std::shared_ptr<vh::storage::Engine> defaultPreviewEngine(const uint32_t vaultId) {
    return vh::runtime::Deps::get().storageManager->getEngine(vaultId);
}

[[nodiscard]] vh::protocols::http::Router::PreviewSessionResolver& previewSessionResolver() {
    static vh::protocols::http::Router::PreviewSessionResolver resolver = defaultPreviewSessionResolver;
    return resolver;
}

[[nodiscard]] vh::protocols::http::Router::SharePreviewManagerFactory& sharePreviewManagerFactory() {
    static vh::protocols::http::Router::SharePreviewManagerFactory factory = defaultSharePreviewManager;
    return factory;
}

[[nodiscard]] vh::protocols::http::Router::SharePreviewResolverFactory& sharePreviewResolverFactory() {
    static vh::protocols::http::Router::SharePreviewResolverFactory factory = defaultSharePreviewResolver;
    return factory;
}

[[nodiscard]] vh::protocols::http::Router::PreviewEngineResolver& previewEngineResolver() {
    static vh::protocols::http::Router::PreviewEngineResolver resolver = defaultPreviewEngine;
    return resolver;
}
}

static std::string url_decode(const std::string& value) {
    std::ostringstream result;
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%' && i + 2 < value.length()) {
            if (int hex = 0; std::istringstream(value.substr(i + 1, 2)) >> std::hex >> hex) {
                result << static_cast<char>(hex);
                i += 2;
            } else throw std::runtime_error("Invalid percent-encoding in URL");
        }
        else if (value[i] == '+') result << ' ';
        else result << value[i];
    }
    return result.str();
}

static std::unordered_map<std::string, std::string> parse_query_params(const std::string& target) {
    std::unordered_map<std::string, std::string> params;

    const auto pos = target.find('?');
    if (pos == std::string::npos) return params;

    const std::string query = target.substr(pos + 1);
    std::istringstream stream(query);
    std::string pair;

    while (std::getline(stream, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq != std::string::npos) {
            const auto key = url_decode(pair.substr(0, eq));
            const auto value = url_decode(pair.substr(eq + 1));
            params[key] = value;
        }
    }

    return params;
}

static bool isShareModeRequestTarget(const std::string& target) {
    const auto params = parse_query_params(target);
    return params.contains("share") && params.at("share") == "1";
}

static std::vector<uint8_t> tryCacheRead(const std::shared_ptr<File>& f, const std::filesystem::path& thumbnailRoot,
                                         const unsigned int size) {
    if (const auto thumbnail_sizes = vh::config::Registry::get().caching.thumbnails.sizes;
        std::ranges::find(thumbnail_sizes.begin(), thumbnail_sizes.end(), size) != thumbnail_sizes.end()) {

        if (const auto pathToJpegCache = thumbnailRoot / f->base32_alias / std::string(std::to_string(size) + ".jpg");
            f->mime_type && (f->mime_type->starts_with("image/") || f->mime_type->starts_with("application/"))
            && std::filesystem::exists(pathToJpegCache))
            return readFileToVector(pathToJpegCache);

        vh::runtime::Deps::get().httpCacheStats->record_miss();
        }
    return {};
}

static std::unique_ptr<vh::protocols::http::model::preview::Request> preparePreviewRequest(
    const std::unordered_map<std::string, std::string>& params
) {
    auto pr = std::make_unique<vh::protocols::http::model::preview::Request>(params);
    pr->engine = previewEngineResolver()(pr->vault_id);
    if (!pr->engine)
        throw std::runtime_error("No storage engine found for vault with id " + std::to_string(pr->vault_id));

    const auto fusePath = pr->engine->paths->absRelToAbsRel(pr->rel_path, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    const auto entry = vh::runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) throw std::runtime_error("File not found in cache");
    if (entry->isDirectory()) throw std::runtime_error("Requested preview file is a directory");
    pr->file = std::static_pointer_cast<File>(entry);
    return pr;
}

static std::unique_ptr<vh::protocols::http::model::preview::Request> prepareSharePreviewRequest(
    const std::unordered_map<std::string, std::string>& params,
    const std::shared_ptr<vh::protocols::ws::Session>& session
) {
    if (!params.contains("share") || params.at("share") != "1")
        throw std::runtime_error("Share preview requests must use the share preview lane");
    if (!session || !session->isShareMode() || session->user)
        throw std::runtime_error("Share preview requires a ready share session");
    if (session->shareSessionToken().empty())
        throw std::runtime_error("Share session token is missing");

    auto manager = sharePreviewManagerFactory()();
    if (!manager) throw std::runtime_error("Share preview manager is unavailable");
    auto resolver = sharePreviewResolverFactory()();
    if (!resolver) throw std::runtime_error("Share preview resolver is unavailable");

    auto principal = manager->resolvePrincipal(
        session->shareSessionToken(),
        session->ipAddress.empty() ? std::nullopt : std::make_optional(session->ipAddress),
        session->userAgent.empty() ? std::nullopt : std::make_optional(session->userAgent)
    );
    session->setSharePrincipal(principal, session->shareSessionToken());

    const auto actor = session->rbacActor();
    const auto target = resolver->resolve(actor, {
        .path = params.contains("path") ? params.at("path") : std::string{"/"},
        .operation = vh::share::Operation::Preview,
        .path_mode = vh::share::TargetPathMode::ShareRelative,
        .expected_target_type = vh::share::TargetType::File
    });

    std::unordered_map<std::string, std::string> scopedParams{
        {"vault_id", std::to_string(target.vault_id)},
        {"path", target.vault_path}
    };
    if (params.contains("size")) scopedParams["size"] = params.at("size");
    if (params.contains("scale")) scopedParams["scale"] = params.at("scale");

    auto pr = std::make_unique<vh::protocols::http::model::preview::Request>(scopedParams);
    pr->engine = previewEngineResolver()(pr->vault_id);
    if (!pr->engine)
        throw std::runtime_error("No storage engine found for scoped share preview");

    const auto file = std::dynamic_pointer_cast<File>(target.entry);
    if (!file) throw std::runtime_error("Share preview target is not a file");
    pr->file = file;

    manager->appendAccessAuditEvent(*principal, {
        .event_type = "share.preview.http",
        .target = {
            .vault_id = target.vault_id,
            .target_entry_id = target.entry ? target.entry->id : 0,
            .target_path = target.vault_path
        },
        .status = vh::share::AuditStatus::Success,
        .bytes_transferred = std::nullopt,
        .error_code = std::nullopt,
        .error_message = std::nullopt
    });

    return pr;
}

constexpr uint64_t kMaxHttpDownloadFileBytes = 256ull * 1024ull * 1024ull;
constexpr uint64_t kMaxArchiveSourceBytes = 256ull * 1024ull * 1024ull;
constexpr uint64_t kMaxArchiveBytes = 320ull * 1024ull * 1024ull;
constexpr uint32_t kMaxArchiveEntries = 4096;
constexpr uint16_t kZipDateJanOne1980 = 33;

[[nodiscard]] bool containsText(const std::string& haystack, const std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

[[nodiscard]] std::string requiredParam(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name
) {
    if (!params.contains(name) || params.at(name).empty())
        throw std::invalid_argument("Missing required parameter: " + name);
    return params.at(name);
}

[[nodiscard]] uint32_t parseUint32Param(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name
) {
    const auto value = requiredParam(params, name);
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed > std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("Invalid numeric parameter: " + name);
    return static_cast<uint32_t>(parsed);
}

[[nodiscard]] std::string requestPathParam(const std::unordered_map<std::string, std::string>& params) {
    if (!params.contains("path") || params.at("path").empty()) return "/";
    return params.at("path");
}

[[nodiscard]] std::string safeAttachmentFilename(std::string value, const std::string& fallback) {
    value = std::filesystem::path(value).filename().string();
    if (value.empty() || value == "." || value == "..") value = fallback;

    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (c < 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '/' || c == ';')
            out.push_back('_');
        else out.push_back(static_cast<char>(c));
    }

    while (!out.empty() && (out.front() == '.' || std::isspace(static_cast<unsigned char>(out.front()))))
        out.erase(out.begin());
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
        out.pop_back();
    return out.empty() ? fallback : out;
}

[[nodiscard]] std::string archiveFilenameFor(const std::string& value) {
    auto filename = safeAttachmentFilename(value, "download");
    if (!filename.ends_with(".zip")) filename += ".zip";
    return filename;
}

[[nodiscard]] std::string contentDispositionValue(const std::string& filename) {
    return "attachment; filename=\"" + safeAttachmentFilename(filename, "download") + "\"";
}

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

[[nodiscard]] std::vector<uint8_t> readDownloadFile(
    const std::shared_ptr<vh::storage::Engine>& engine,
    const std::shared_ptr<File>& file
) {
    if (!engine || !file) throw std::runtime_error("Download file is unavailable");
    if (file->size_bytes > kMaxHttpDownloadFileBytes)
        throw std::runtime_error("Download file exceeds maximum size");
    if (file->size_bytes == 0) return {};

    std::vector<uint8_t> bytes;
    if (engine->type() == vh::storage::StorageType::Cloud) {
        const auto cloud = std::dynamic_pointer_cast<vh::storage::CloudEngine>(engine);
        if (!cloud) throw std::runtime_error("Cloud storage engine is unavailable");
        bytes = cloud->downloadToBuffer(file->path);
        if (cloud->remoteFileIsEncrypted(file->path))
            bytes = cloud->decrypt(engine->vault->id, file->path, bytes);
    } else {
        bytes = engine->decrypt(file);
    }

    if (bytes.size() > kMaxHttpDownloadFileBytes)
        throw std::runtime_error("Download file exceeds maximum size");
    return bytes;
}

void enforceHumanPermission(
    const std::shared_ptr<vh::protocols::ws::Session>& session,
    const std::shared_ptr<vh::storage::Engine>& engine,
    const std::filesystem::path& vaultPath,
    const vh::rbac::permission::vault::FilesystemAction action
) {
    if (!session || !session->user)
        throw std::runtime_error("Download requires a user session");
    if (!engine || !engine->vault)
        throw std::runtime_error("Download storage engine is unavailable");

    const auto fusePath = engine->paths->absRelToAbsRel(vaultPath, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    if (!vh::rbac::resolver::Vault::has<vh::rbac::permission::vault::FilesystemAction>({
        .user = session->user,
        .permission = action,
        .vault_id = engine->vault->id,
        .path = fusePath
    })) throw std::runtime_error("Permission denied");
}

void enforceHumanDownloadPermissions(
    const std::shared_ptr<vh::protocols::ws::Session>& session,
    const std::shared_ptr<vh::storage::Engine>& engine,
    const std::shared_ptr<Entry>& entry
) {
    if (!entry) throw std::runtime_error("Download target is unavailable");
    using Action = vh::rbac::permission::vault::FilesystemAction;
    enforceHumanPermission(session, engine, entry->path, Action::Read);
    if (entry->isDirectory())
        enforceHumanPermission(session, engine, entry->path, Action::List);
}

struct HumanDownloadTarget {
    std::shared_ptr<vh::storage::Engine> engine;
    std::shared_ptr<Entry> entry;
};

[[nodiscard]] HumanDownloadTarget prepareHumanDownloadTarget(
    const std::unordered_map<std::string, std::string>& params,
    const std::shared_ptr<vh::protocols::ws::Session>& session
) {
    if (!session || !session->user)
        throw std::runtime_error("Download requires a user session");

    const auto vaultId = parseUint32Param(params, "vault_id");
    const auto vaultPath = std::filesystem::path(requestPathParam(params));
    const auto engine = previewEngineResolver()(vaultId);
    if (!engine) throw std::runtime_error("No storage engine found for vault with id " + std::to_string(vaultId));

    const auto fusePath = engine->paths->absRelToAbsRel(vaultPath, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    const auto entry = vh::runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) throw std::runtime_error("Download target not found");

    enforceHumanDownloadPermissions(session, engine, entry);
    return {.engine = engine, .entry = entry};
}

struct ShareDownloadTarget {
    std::shared_ptr<vh::share::Manager> manager;
    std::shared_ptr<vh::share::TargetResolver> resolver;
    std::shared_ptr<vh::share::Principal> principal;
    vh::share::ResolvedTarget target;
    std::shared_ptr<vh::storage::Engine> engine;
};

[[nodiscard]] ShareDownloadTarget prepareShareDownloadTarget(
    const std::unordered_map<std::string, std::string>& params,
    const std::shared_ptr<vh::protocols::ws::Session>& session
) {
    if (!params.contains("share") || params.at("share") != "1")
        throw std::runtime_error("Share download requests must use the share download lane");
    if (!session || !session->isShareMode() || session->user)
        throw std::runtime_error("Share download requires a ready share session");
    if (session->shareSessionToken().empty())
        throw std::runtime_error("Share session token is missing");

    auto manager = sharePreviewManagerFactory()();
    if (!manager) throw std::runtime_error("Share download manager is unavailable");
    auto resolver = sharePreviewResolverFactory()();
    if (!resolver) throw std::runtime_error("Share download resolver is unavailable");

    auto principal = manager->resolvePrincipal(
        session->shareSessionToken(),
        session->ipAddress.empty() ? std::nullopt : std::make_optional(session->ipAddress),
        session->userAgent.empty() ? std::nullopt : std::make_optional(session->userAgent)
    );
    session->setSharePrincipal(principal, session->shareSessionToken());

    const auto actor = session->rbacActor();
    const auto path = requestPathParam(params);
    auto target = resolver->resolve(actor, {
        .path = path,
        .operation = vh::share::Operation::Download,
        .path_mode = vh::share::TargetPathMode::ShareRelative
    });

    if (target.target_type == vh::share::TargetType::Directory) {
        (void)resolver->resolve(actor, {
            .path = path,
            .operation = vh::share::Operation::List,
            .path_mode = vh::share::TargetPathMode::ShareRelative,
            .expected_target_type = vh::share::TargetType::Directory
        });
    }

    auto engine = previewEngineResolver()(target.vault_id);
    if (!engine) throw std::runtime_error("No storage engine found for scoped share download");

    return {
        .manager = std::move(manager),
        .resolver = std::move(resolver),
        .principal = std::move(principal),
        .target = std::move(target),
        .engine = std::move(engine)
    };
}

void addFileToArchive(
    ZipBuilder& zip,
    uint64_t& sourceBytes,
    const std::shared_ptr<vh::storage::Engine>& engine,
    const std::shared_ptr<File>& file,
    const std::string& archivePath
) {
    if (!file) throw std::runtime_error("Archive file is unavailable");
    if (sourceBytes + file->size_bytes > kMaxArchiveSourceBytes)
        throw std::runtime_error("Archive source bytes exceed limit");

    auto bytes = readDownloadFile(engine, file);
    sourceBytes += bytes.size();
    zip.addFile(archivePath, bytes);
}

void addHumanDirectoryToArchive(
    ZipBuilder& zip,
    uint64_t& sourceBytes,
    const std::shared_ptr<vh::protocols::ws::Session>& session,
    const std::shared_ptr<vh::storage::Engine>& engine,
    const std::shared_ptr<Entry>& directory,
    const std::string& rootVaultPath
) {
    if (!directory || !directory->isDirectory()) throw std::runtime_error("Archive target is not a directory");
    enforceHumanDownloadPermissions(session, engine, directory);

    const auto children = vh::runtime::Deps::get().fsCache->listDir(directory->id, false);
    for (const auto& child : children) {
        if (!child) throw std::runtime_error("Archive child is unavailable");
        enforceHumanDownloadPermissions(session, engine, child);
        const auto rel = relativeArchivePath(rootVaultPath, child->path.string());

        if (child->isDirectory()) {
            zip.addDirectory(rel);
            addHumanDirectoryToArchive(zip, sourceBytes, session, engine, child, rootVaultPath);
        } else {
            addFileToArchive(zip, sourceBytes, engine, std::dynamic_pointer_cast<File>(child), rel);
        }
    }
}

void addShareDirectoryToArchive(
    ZipBuilder& zip,
    uint64_t& sourceBytes,
    const ShareDownloadTarget& download,
    const vh::share::ResolvedTarget& directoryTarget,
    const std::string& rootVaultPath
) {
    if (!directoryTarget.entry || directoryTarget.target_type != vh::share::TargetType::Directory)
        throw std::runtime_error("Archive target is not a directory");

    const auto actor = vh::rbac::Actor::share(download.principal);
    auto listTarget = download.resolver->resolve(actor, {
        .path = directoryTarget.share_path,
        .operation = vh::share::Operation::List,
        .path_mode = vh::share::TargetPathMode::ShareRelative,
        .expected_target_type = vh::share::TargetType::Directory
    });

    const auto children = download.resolver->listChildren(actor, listTarget);
    for (const auto& child : children) {
        if (!child) throw std::runtime_error("Archive child is unavailable");
        const auto sharePath = vh::share::TargetResolver::shareRelativePath(*download.principal, child->path.string());
        const auto rel = relativeArchivePath(rootVaultPath, child->path.string());

        if (child->isDirectory()) {
            auto childTarget = download.resolver->resolve(actor, {
                .path = sharePath,
                .operation = vh::share::Operation::Download,
                .path_mode = vh::share::TargetPathMode::ShareRelative,
                .expected_target_type = vh::share::TargetType::Directory
            });
            zip.addDirectory(rel);
            addShareDirectoryToArchive(zip, sourceBytes, download, childTarget, rootVaultPath);
        } else {
            auto childTarget = download.resolver->resolve(actor, {
                .path = sharePath,
                .operation = vh::share::Operation::Download,
                .path_mode = vh::share::TargetPathMode::ShareRelative,
                .expected_target_type = vh::share::TargetType::File
            });
            addFileToArchive(zip, sourceBytes, download.engine, std::dynamic_pointer_cast<File>(childTarget.entry), rel);
        }
    }
}

[[nodiscard]] std::vector<uint8_t> buildHumanDirectoryArchive(
    const HumanDownloadTarget& target,
    const std::shared_ptr<vh::protocols::ws::Session>& session
) {
    ZipBuilder zip;
    uint64_t sourceBytes = 0;
    addHumanDirectoryToArchive(zip, sourceBytes, session, target.engine, target.entry, target.entry->path.string());
    return std::move(zip).finish();
}

[[nodiscard]] std::vector<uint8_t> buildShareDirectoryArchive(const ShareDownloadTarget& target) {
    ZipBuilder zip;
    uint64_t sourceBytes = 0;
    addShareDirectoryToArchive(zip, sourceBytes, target, target.target, target.target.vault_path);
    return std::move(zip).finish();
}

void recordShareDownloadSuccess(
    const ShareDownloadTarget& download,
    const uint64_t bytes
) {
    if (!download.manager || !download.principal || !download.target.entry) return;
    download.manager->incrementDownloadCount(*download.principal);
    download.manager->appendAccessAuditEvent(*download.principal, {
        .event_type = "share.download.http",
        .target = {
            .vault_id = download.target.vault_id,
            .target_entry_id = download.target.entry->id,
            .target_path = download.target.vault_path
        },
        .status = vh::share::AuditStatus::Success,
        .bytes_transferred = bytes,
        .error_code = std::nullopt,
        .error_message = std::nullopt
    });
}

[[nodiscard]] boost::beast::http::status downloadErrorStatus(const std::exception& e) {
    const std::string message = e.what();
    if (containsText(message, "requires a user session") ||
        containsText(message, "requires a ready share session") ||
        containsText(message, "token") ||
        containsText(message, "Unauthorized"))
        return boost::beast::http::status::unauthorized;
    if (containsText(message, "denied") || containsText(message, "Permission"))
        return boost::beast::http::status::forbidden;
    if (containsText(message, "not found") || containsText(message, "missing") || containsText(message, "moved"))
        return boost::beast::http::status::not_found;
    if (containsText(message, "exceeds") || containsText(message, "limit"))
        return boost::beast::http::status::payload_too_large;
    return boost::beast::http::status::bad_request;
}
namespace vh::protocols::http {

using namespace vh::protocols::http::model::preview;

Response Router::route(request&& req) {
    if (req.method() != verb::get)
        return makeErrorResponse(req, "Invalid request", status::bad_request);

    if (req.target().starts_with("/auth/session"))
        return handleAuthSession(std::move(req));

    if (req.target().starts_with("/preview"))
        return handlePreview(std::move(req));

    if (req.target().starts_with("/download"))
        return handleDownload(std::move(req));

    return makeErrorResponse(req, "Not found", status::not_found);
}

Response Router::handleAuthSession(request&& req) {
    if (vh::config::Registry::get().dev.enabled) return makeJsonResponse(req, nlohmann::json{{"ok", true}});

    try {
        const auto refresh = protocols::extractCookie(req, "refresh");
        if (refresh.empty()) {
            log::Registry::http()->warn("[Router] Refresh token not set");
            return makeErrorResponse(req, "Refresh token not set", status::bad_request);
        }
        const auto session = runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh);

        // Could mint an access token here if we wanted to
        nlohmann::json j{
                    {"ok", true},
                    {"user_id", session->user->id},
                    // {"access_token", access.token},
                    // {"expires_in", access.expires_in}
                };

        return makeJsonResponse(req, j);
    } catch (const std::exception& e) {
        log::Registry::http()->warn("[Router]: Invalid refresh token: %s", e.what());
        return makeErrorResponse(req, std::string("Unauthorized: ") + e.what(), status::unauthorized);
    }
}

std::string Router::authenticateRequest(const request& req) {
    if (vh::config::Registry::get().dev.enabled) return "";
    try {
        const auto refresh_token = protocols::extractCookie(req, "refresh");
        runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh_token);
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

void Router::setPreviewSessionResolverForTesting(PreviewSessionResolver resolver) {
    if (!resolver) throw std::invalid_argument("Preview session resolver is required");
    previewSessionResolver() = std::move(resolver);
}

void Router::resetPreviewSessionResolverForTesting() {
    previewSessionResolver() = defaultPreviewSessionResolver;
}

void Router::setSharePreviewManagerFactoryForTesting(SharePreviewManagerFactory factory) {
    if (!factory) throw std::invalid_argument("Share preview manager factory is required");
    sharePreviewManagerFactory() = std::move(factory);
}

void Router::resetSharePreviewManagerFactoryForTesting() {
    sharePreviewManagerFactory() = defaultSharePreviewManager;
}

void Router::setSharePreviewResolverFactoryForTesting(SharePreviewResolverFactory factory) {
    if (!factory) throw std::invalid_argument("Share preview resolver factory is required");
    sharePreviewResolverFactory() = std::move(factory);
}

void Router::resetSharePreviewResolverFactoryForTesting() {
    sharePreviewResolverFactory() = defaultSharePreviewResolver;
}

void Router::setPreviewEngineResolverForTesting(PreviewEngineResolver resolver) {
    if (!resolver) throw std::invalid_argument("Preview engine resolver is required");
    previewEngineResolver() = std::move(resolver);
}

void Router::resetPreviewEngineResolverForTesting() {
    previewEngineResolver() = defaultPreviewEngine;
}

Response Router::handlePreview(request&& req) {
    if (req.method() != verb::get || !req.target().starts_with("/preview"))
        return makeErrorResponse(
            req, "Invalid request", status::bad_request);

    const auto params = parse_query_params(std::string(req.target()));
    const auto sharePreview = isShareModeRequestTarget(std::string(req.target()));

    std::shared_ptr<vh::protocols::ws::Session> session;
    try {
        session = previewSessionResolver()(req);
    } catch (const std::exception& e) {
        return makeErrorResponse(req, "Unauthorized: " + std::string(e.what()), status::unauthorized);
    }

    std::unique_ptr<Request> pr;
    try {
        if (sharePreview) {
            if (!session || !session->isShareMode() || session->user)
                return makeErrorResponse(req, "Unauthorized: preview requires a ready share session", status::unauthorized);
            pr = prepareSharePreviewRequest(params, session);
        }
        else {
            if (!session || !session->user)
                return makeErrorResponse(req, "Unauthorized: preview requires a user", status::unauthorized);
            pr = preparePreviewRequest(params);
        }
    } catch (const std::exception& e) {
        return makeErrorResponse(req, e.what(), status::bad_request);
    }

    if (!pr->file->mime_type)
        return makeErrorResponse(req, "File has no mime type.", status::unsupported_media_type);

    if (pr->size)
        if (auto data = tryCacheRead(pr->file, pr->engine->paths->thumbnailRoot, *pr->size); !data.empty())
            return makeResponse(req, std::move(data), "image/jpeg", true);

    if (pr->file->mime_type->starts_with("image/") || pr->file->mime_type->ends_with("/pdf")) {
        ScopedOpTimer timer(runtime::Deps::get().httpCacheStats.get());
        return pr->file->mime_type->starts_with("image/")
                   ? handler::preview::Image::handle(std::move(req), std::move(pr))
                   : handler::preview::Pdf::handle(std::move(req), std::move(pr));
    }

    return makeErrorResponse(
        req, "Unsupported preview type: " + (pr->file->mime_type ? *pr->file->mime_type : "unknown"));
}

Response Router::handleDownload(request&& req) {
    if (req.method() != verb::get || !req.target().starts_with("/download"))
        return makeErrorResponse(req, "Invalid request", status::bad_request);

    const auto params = parse_query_params(std::string(req.target()));
    const auto shareDownload = isShareModeRequestTarget(std::string(req.target()));

    std::shared_ptr<vh::protocols::ws::Session> session;
    try {
        session = previewSessionResolver()(req);
    } catch (const std::exception& e) {
        return makeErrorResponse(req, "Unauthorized: " + std::string(e.what()), status::unauthorized);
    }

    try {
        if (shareDownload) {
            if (!session || !session->isShareMode() || session->user)
                return makeErrorResponse(req, "Unauthorized: download requires a ready share session", status::unauthorized);

            auto target = prepareShareDownloadTarget(params, session);
            if (!target.target.entry) throw std::runtime_error("Share download target not found");

            if (target.target.target_type == vh::share::TargetType::Directory) {
                auto data = buildShareDirectoryArchive(target);
                const auto bytes = data.size();
                auto response = makeDownloadResponse(
                    req,
                    std::move(data),
                    "application/zip",
                    archiveFilenameFor(target.target.entry->name)
                );
                recordShareDownloadSuccess(target, bytes);
                return response;
            }

            auto file = std::dynamic_pointer_cast<File>(target.target.entry);
            auto data = readDownloadFile(target.engine, file);
            const auto bytes = data.size();
            const auto mime = file && file->mime_type ? *file->mime_type : std::string{"application/octet-stream"};
            auto response = makeDownloadResponse(req, std::move(data), mime, file ? file->name : "download");
            recordShareDownloadSuccess(target, bytes);
            return response;
        }

        if (!session || !session->user)
            return makeErrorResponse(req, "Unauthorized: download requires a user", status::unauthorized);

        auto target = prepareHumanDownloadTarget(params, session);
        if (!target.entry) throw std::runtime_error("Download target not found");

        if (target.entry->isDirectory()) {
            auto data = buildHumanDirectoryArchive(target, session);
            return makeDownloadResponse(
                req,
                std::move(data),
                "application/zip",
                archiveFilenameFor(target.entry->name)
            );
        }

        auto file = std::dynamic_pointer_cast<File>(target.entry);
        auto data = readDownloadFile(target.engine, file);
        const auto mime = file && file->mime_type ? *file->mime_type : std::string{"application/octet-stream"};
        return makeDownloadResponse(req, std::move(data), mime, file ? file->name : "download");
    } catch (const std::exception& e) {
        return makeErrorResponse(req, e.what(), downloadErrorStatus(e));
    }
}

Response Router::makeResponse(const request& req, std::vector<uint8_t>&& data,
                                         const std::string& mime_type, const bool cacheHit) {
    const auto size = data.size();

    vector_response res{
        std::piecewise_construct,
        std::make_tuple(std::move(data)),
        std::make_tuple(status::ok, req.version())
    };

    res.set(field::content_type, mime_type);
    res.content_length(size);
    res.keep_alive(req.keep_alive());

    if (cacheHit) runtime::Deps::get().httpCacheStats->record_hit(size);

    return res;
}

Response Router::makeResponse(const request& req, file_body::value_type data,
                                         const std::string& mime_type, const bool cacheHit) {
    const auto size = data.size();

    file_response res{
        std::piecewise_construct,
        std::make_tuple(std::move(data)),
        std::make_tuple(status::ok, req.version())
    };

    res.set(field::content_type, mime_type);
    res.content_length(size);
    res.keep_alive(req.keep_alive());

    if (cacheHit) runtime::Deps::get().httpCacheStats->record_hit(size);

    return res;
}

Response Router::makeJsonResponse(const request& req, const nlohmann::json& j) {
    string_response res{status::ok, req.version()};
    res.set(field::content_type, "application/json");
    res.body() = j.dump();
    res.prepare_payload();
    res.keep_alive(req.keep_alive());
    return res;
}

Response Router::makeDownloadResponse(
    const request& req,
    std::vector<uint8_t>&& data,
    const std::string& mime_type,
    const std::string& filename
) {
    const auto size = data.size();

    vector_response res{
        std::piecewise_construct,
        std::make_tuple(std::move(data)),
        std::make_tuple(status::ok, req.version())
    };

    res.set(field::content_type, mime_type.empty() ? "application/octet-stream" : mime_type);
    res.set(field::content_disposition, contentDispositionValue(filename));
    res.set(field::cache_control, "no-store");
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
}

Response Router::makeErrorResponse(const request& req,
                                              const std::string& msg,
                                              const status& status) {
    string_response res{status, req.version()};
    res.set(field::content_type, "text/plain");
    res.body() = msg;
    res.prepare_payload();
    return res;
}

}
