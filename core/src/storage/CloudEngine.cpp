#include "storage/CloudEngine.hpp"
#include "vault/EncryptionManager.hpp"
#include "storage/s3/Controller.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "fs/model/file/Trashed.hpp"
#include "fs/model/Directory.hpp"
#include "vault/model/S3Vault.hpp"
#include "fs/model/Path.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "fs/ops/file.hpp"
#include "preview/thumbnail/Worker.hpp"
#include "fs/Filesystem.hpp"
#include "runtime/Deps.hpp"
#include "vault/APIKeyManager.hpp"
#include "config/Registry.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/RemoteManifest.hpp"

#include <algorithm>
#include <cctype>

using namespace vh::fs;
using namespace vh::fs::model;
using namespace vh::storage;
using namespace vh::vault;
using namespace vh::concurrency;
using namespace vh::config;
using namespace vh::sync::model;
using namespace vh::fs::ops;

static constexpr std::string_view META_VH_ENCRYPTED_FLAG = "vh-encrypted";
static constexpr std::string_view META_VH_IV_FLAG = "vh-iv";
static constexpr std::string_view META_VH_KEY_VERSION_FLAG = "vh-key-version";
static constexpr std::string_view META_CONTENT_HASH_FLAG = "content-hash";
static constexpr std::string_view META_VH_ENCRYPTED = "x-amz-meta-vh-encrypted";
static constexpr std::string_view META_VH_IV = "x-amz-meta-vh-iv";
static constexpr std::string_view META_VH_KEY_VERSION = "x-amz-meta-vh-key-version";
static constexpr std::string_view META_CONTENT_HASH = "x-amz-meta-content-hash";

namespace {
    std::optional<std::string> header_value(
        const std::unordered_map<std::string, std::string>& headers,
        const std::string_view name) {
        auto target = std::string(name);
        std::ranges::transform(target, target.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        for (const auto& [key, value] : headers) {
            auto normalized = key;
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (normalized == target) return value;
        }

        return std::nullopt;
    }

    bool contains_case_insensitive(std::string haystack, std::string needle) {
        std::ranges::transform(haystack, haystack.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::ranges::transform(needle, needle.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return haystack.find(needle) != std::string::npos;
    }
}

std::unordered_map<std::string, std::string> CloudEngine::getMetaMapFromFile(const std::shared_ptr<File>& f) const {
    std::unordered_map<std::string, std::string> meta;
    const auto encrypt = s3Vault()->encrypt_upstream;
    meta[std::string(META_VH_ENCRYPTED_FLAG)] = encrypt ? "true" : "false";
    if (f->content_hash) meta[std::string(META_CONTENT_HASH_FLAG)] = *f->content_hash;
    if (encrypt) {
        meta[std::string(META_VH_KEY_VERSION_FLAG)] = std::to_string(f->encrypted_with_key_version);
        meta[std::string(META_VH_IV_FLAG)] = f->encryption_iv;
    }

    return meta;
}

CloudEngine::CloudEngine(const std::shared_ptr<S3Vault>& vault)
    : Engine(vault),
      key_(runtime::Deps::get().apiKeyManager->getAPIKey(vault->api_key_id, vault->owner_id)),
      s3Provider_(std::make_shared<s3::Controller>(key_, vault->bucket)) {}

CloudEngine::CloudEngine(const std::shared_ptr<S3Vault>& vault, std::shared_ptr<s3::Controller> s3Provider)
    : Engine(vault),
      key_(runtime::Deps::get().apiKeyManager->getAPIKey(vault->api_key_id, vault->owner_id)),
      s3Provider_(std::move(s3Provider)) {}

void CloudEngine::upload(const std::shared_ptr<File>& f) const {
    if (!fs::exists(f->backing_path) || !fs::is_regular_file(f->backing_path))
        throw std::runtime_error("[CloudStorageEngine] Invalid file: " + f->path.string());

    if (!s3Vault()->encrypt_upstream) {
        const auto ciphertext = readFileToVector(f->backing_path);
        upload(f, ciphertext);
        return;
    }

    const fs::path s3Key = stripLeadingSlash(f->path);

    if (!f->content_hash) f->content_hash = db::query::fs::File::getContentHash(vault->id, f->path);
    const auto meta = getMetaMapFromFile(f);

    if (fs::file_size(f->backing_path) < s3::Controller::MIN_PART_SIZE)
        s3Provider_->uploadObjectWithMetadata(s3Key, f->backing_path, meta);
    else
        s3Provider_->uploadLargeObject(s3Key, f->backing_path, s3::Controller::MIN_PART_SIZE, meta);
}

void CloudEngine::upload(const std::shared_ptr<File>& f, const std::vector<uint8_t>& buffer, const bool isCiphertext) const {
    if (buffer.empty()) throw std::invalid_argument("[CloudStorageEngine] Buffer for upload cannot be empty");
    if (!f) throw std::invalid_argument("[CloudStorageEngine] Invalid file or buffer for upload");

    if (!s3Vault()->encrypt_upstream) {
        const auto plaintext = isCiphertext ? decrypt(f, buffer) : buffer;
        s3Provider_->uploadBufferWithMetadata(stripLeadingSlash(f->path), plaintext, getMetaMapFromFile(f));
        return;
    }

    const auto s3Key = stripLeadingSlash(f->path);
    const auto meta = getMetaMapFromFile(f);

    if (buffer.size() < s3::Controller::MIN_PART_SIZE) s3Provider_->uploadBufferWithMetadata(s3Key, buffer, meta);
    else s3Provider_->uploadLargeObject(s3Key, buffer, s3::Controller::MIN_PART_SIZE, meta);
}

std::vector<uint8_t> CloudEngine::downloadToBuffer(const fs::path& rel_path) const {
    std::vector<uint8_t> buffer;
    s3Provider_->downloadToBuffer(stripLeadingSlash(rel_path), buffer);
    return buffer;
}

std::shared_ptr<File> CloudEngine::downloadFile(const fs::path& rel_path) {
    auto buffer = downloadToBuffer(rel_path);
    const auto s3Key = stripLeadingSlash(rel_path);

    if (remoteFileIsEncrypted(rel_path)) {
        auto payload = getRemoteIVBase64AndVersion(rel_path);
        if (!payload) payload = db::query::fs::File::getEncryptionIVAndVersion(vault->id, rel_path);
        if (!payload) throw std::runtime_error("[CloudStorageEngine] No IV found for encrypted file: " + rel_path.string());
        const auto& [iv_b64, key_version] = *payload;
        buffer = encryptionManager->decrypt(buffer, iv_b64, key_version);
    }

    const auto owner = db::query::identities::User::getUserById(vault->owner_id);
    if (!owner) throw std::runtime_error("[CloudStorageEngine] Vault owner not found for file creation");

    const auto f = Filesystem::createFile({
            .path = makeAbsolute(rel_path),
            .fuse_path = vaultPathToFusePath(makeAbsolute(rel_path)),
            .buffer = buffer,
            .engine = shared_from_this(),
            .user = owner,
            .overwrite = true
        });

    preview::thumbnail::Worker::enqueue(shared_from_this(), buffer, f);

    return f;
}

void CloudEngine::indexAndDeleteFile(const std::shared_ptr<File>& remoteFile) {
    if (!remoteFile) throw std::invalid_argument("[CloudStorageEngine] Cannot index null remote file");

    const auto owner = db::query::identities::User::getUserById(vault->owner_id);
    if (!owner) throw std::runtime_error("[CloudStorageEngine] Vault owner not found for metadata-only indexing");

    const auto rel_path = makeAbsolute(remoteFile->path);
    auto indexed = db::query::fs::File::getFileByPath(vault->id, rel_path);

    if (!indexed) {
        indexed = Filesystem::createFile({
            .path = rel_path,
            .fuse_path = vaultPathToFusePath(rel_path),
            .buffer = {},
            .engine = shared_from_this(),
            .user = owner,
            .overwrite = true
        });
    }

    indexed->size_bytes = remoteFile->size_bytes;
    indexed->content_hash = remoteFile->content_hash;
    indexed->updated_at = remoteFile->updated_at;
    indexed->last_modified_by = vault->owner_id;
    db::query::fs::File::updateFile(indexed);

    if (!indexed->backing_path.empty() && fs::exists(indexed->backing_path))
        fs::remove(indexed->backing_path);
}

std::unordered_map<std::u8string, std::shared_ptr<File>>
CloudEngine::getGroupedFilesFromS3(const fs::path& prefix) const {
    return groupEntriesByPath(filesFromS3XML(s3Provider_->listObjects(prefix)));
}

bool CloudEngine::refreshRemoteIndexFromManifestIfChanged() const {
    const std::string manifestKey = remote_manifest::INDEX_V1_KEY;
    const auto head = s3Provider_->getHeadObject(manifestKey);
    if (!head) return false;

    const auto remoteEtag = header_value(*head, "ETag");
    const auto localEtag = db::query::sync::RemoteObjectIndex::getManifestETag(vault->id, manifestKey);
    if (remoteEtag && localEtag && *remoteEtag == *localEtag &&
        db::query::sync::RemoteObjectIndex::countForVault(vault->id) > 0)
        return true;

    std::vector<uint8_t> buffer;
    s3Provider_->downloadToBuffer(manifestKey, buffer);
    const std::string manifest(buffer.begin(), buffer.end());
    const auto files = remote_manifest::parseIndexV1(manifest);
    db::query::sync::RemoteObjectIndex::replaceFromManifest(vault->id, files);
    db::query::sync::RemoteObjectIndex::upsertManifestETag(vault->id, manifestKey, remoteEtag);
    return true;
}

void CloudEngine::publishRemoteIndexManifest() const {
    const auto files = db::query::sync::RemoteObjectIndex::listFilesForVault(vault->id);
    const auto manifest = remote_manifest::buildIndexV1(vault->id, files);
    const std::vector<uint8_t> payload(manifest.begin(), manifest.end());

    s3Provider_->uploadBufferWithMetadata(
        remote_manifest::INDEX_V1_KEY,
        payload,
        {{"vh-manifest-version", std::to_string(remote_manifest::INDEX_V1_VERSION)}});

    const auto head = s3Provider_->getHeadObject(remote_manifest::INDEX_V1_KEY);
    db::query::sync::RemoteObjectIndex::upsertManifestETag(
        vault->id,
        remote_manifest::INDEX_V1_KEY,
        head ? header_value(*head, "ETag") : std::nullopt);
}

void CloudEngine::applyRemoteIndexMutation(const std::vector<Action>& plan) const {
    bool mutated = false;

    for (const auto& action : plan) {
        switch (action.type) {
        case ActionType::Upload:
            if (action.local) {
                db::query::sync::RemoteObjectIndex::upsertFile(vault->id, action.local);
                mutated = true;
            }
            break;
        case ActionType::DeleteRemote:
            if (action.remote) {
                db::query::sync::RemoteObjectIndex::deleteKey(vault->id, action.remote->path);
                mutated = true;
            }
            break;
        default:
            break;
        }
    }

    if (mutated) publishRemoteIndexManifest();
}

bool CloudEngine::selectedDownloadRequiresRestore(const std::shared_ptr<File>& remoteFile) const {
    if (!remoteFile) return false;
    if (remoteFile->requiresArchiveRestoreForBodyGet()) return true;

    if (!remoteFile->remote_storage_class) return false;
    auto storageClass = *remoteFile->remote_storage_class;
    std::ranges::transform(storageClass, storageClass.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (storageClass != "INTELLIGENT_TIERING")
        return false;

    const auto head = s3Provider_->getHeadObject(stripLeadingSlash(remoteFile->path));
    if (!head) return true;

    if (const auto archiveStatus = header_value(*head, "x-amz-archive-status");
        archiveStatus && !archiveStatus->empty())
        return true;

    if (const auto restore = header_value(*head, "x-amz-restore");
        restore && contains_case_insensitive(*restore, "ongoing-request=\"true\""))
        return true;

    return false;
}

std::string CloudEngine::getRemoteContentHash(const fs::path& rel_path) const {
    if (const auto head = s3Provider_->getHeadObject(stripLeadingSlash(rel_path)))
        if (head->contains(std::string(META_CONTENT_HASH))) return head->at(std::string(META_CONTENT_HASH));
    return "";
}

bool CloudEngine::remoteFileIsEncrypted(const fs::path& rel_path) const {
    const auto s3Key = stripLeadingSlash(rel_path);

    if (const auto head = s3Provider_->getHeadObject(s3Key)) {
        if (head->contains(std::string(META_VH_ENCRYPTED))) {
            const std::string& val = head->at(std::string(META_VH_ENCRYPTED));
            return val == "true" || val == "1";
        }
    }

    return false; // assume unencrypted if metadata is missing or head request failed
}

std::vector<std::shared_ptr<Directory>> CloudEngine::extractDirectories(
    const std::vector<std::shared_ptr<File>>& files) const {

    std::unordered_map<std::u8string, std::shared_ptr<Directory>> directories;

    for (const auto& file : files) {
        fs::path current = "/";

        for (const auto& part : file->path.parent_path()) {
            current /= part;

            if (!directories.contains(current.u8string())) {
                const auto dir = std::make_shared<Directory>();
                dir->path = current;
                dir->name = current.filename().string();
                dir->created_by = dir->last_modified_by = vault->owner_id;
                dir->vault_id = vault->id;
                const auto parent_path = current.has_parent_path() ? current.parent_path() : "/";
                dir->parent_id = db::query::fs::Directory::getDirectoryIdByPath(vault->id, parent_path);

                directories[current.u8string()] = dir;
            }
        }
    }

    std::vector<std::shared_ptr<Directory>> result;
    for (const auto& [_, dir] : directories)
        result.push_back(dir);

    std::ranges::sort(result, [](const auto& a, const auto& b) {
        return std::distance(a->path.begin(), a->path.end()) < std::distance(b->path.begin(), b->path.end());
    });

    return result;
}

std::optional<std::pair<std::string, unsigned int>> CloudEngine::getRemoteIVBase64AndVersion(const fs::path& rel_path) const {
    const fs::path s3Key = stripLeadingSlash(rel_path);

    std::string iv_b64;
    unsigned int key_version = 0;

    if (const auto head = s3Provider_->getHeadObject(s3Key)) {
        if (head->contains(std::string(META_VH_IV))) iv_b64 = head->at(std::string(META_VH_IV));
        if (head->contains(std::string(META_VH_KEY_VERSION)))
            key_version = std::stoul(head->at(std::string(META_VH_KEY_VERSION)));
    }

    if (iv_b64.empty() || key_version == 0) {
        log::Registry::cloud()->error("[CloudStorageEngine] No IV or key version found for encrypted file: {}", rel_path.string());
        return std::nullopt;
    }

    return std::make_pair(iv_b64, key_version);
}

void CloudEngine::purge(const fs::path& rel_path) const {
    removeLocally(rel_path);
    removeRemotely(rel_path, true);
}

void CloudEngine::purge(const std::shared_ptr<file::Trashed>& f) const {
    removeLocally(f);
    removeRemotely(f, true);
}

void CloudEngine::removeRemotely(const fs::path& rel_path, const bool rmThumbnails) const {
    s3Provider_->deleteObject(stripLeadingSlash(rel_path));
    if (rmThumbnails) purgeThumbnails(rel_path);
}

void CloudEngine::removeRemotely(const std::shared_ptr<file::Trashed>& f, bool rmThumbnails) const {
    const auto vaultPath = fusePathToVaultPath(f->path);
    s3Provider_->deleteObject(stripLeadingSlash(vaultPath));
    if (rmThumbnails) purgeThumbnails(vaultPath);
}

std::shared_ptr<S3Vault> CloudEngine::s3Vault() const { return std::static_pointer_cast<S3Vault>(vault); }

std::shared_ptr<RemotePolicy> CloudEngine::remote_policy() const {
    return std::static_pointer_cast<RemotePolicy>(sync);
}

void CloudEngine::configureS3RequestBudget(const s3::S3RequestBudget& budget) const {
    if (s3Provider_) s3Provider_->setRequestBudget(budget);
}

void CloudEngine::clearS3RequestBudget() const {
    if (s3Provider_) s3Provider_->clearRequestBudget();
}

void CloudEngine::resetS3RequestMetrics() const {
    if (s3Provider_) s3Provider_->resetRequestMetrics();
}

s3::S3RequestMetrics CloudEngine::s3RequestMetrics() const {
    if (!s3Provider_) return {};
    return s3Provider_->requestMetrics();
}

void CloudEngine::setS3ControllerForTesting(std::shared_ptr<s3::Controller> s3Provider) {
    s3Provider_ = std::move(s3Provider);
}
