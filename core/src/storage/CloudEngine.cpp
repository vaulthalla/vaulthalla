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
#include "sync/model/Event.hpp"

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
    constexpr int REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS = 3;

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

    fs::path resolvedBackingPath(const CloudEngine& engine, const std::shared_ptr<File>& file) {
        if (!file) throw std::invalid_argument("[CloudStorageEngine] Invalid file");
        if (file->backing_path.is_absolute()) return file->backing_path;
        if (!engine.paths) return file->backing_path;
        return engine.paths->absPath(file->backing_path, PathType::BACKING_ROOT);
    }
}

std::unordered_map<std::string, std::string> CloudEngine::getMetaMapFromFile(const std::shared_ptr<File>& f) const {
    std::unordered_map<std::string, std::string> meta;
    const auto encrypt = s3Vault()->encrypt_upstream;
    meta[std::string(META_VH_ENCRYPTED_FLAG)] = encrypt ? "true" : "false";
    if (f->content_hash) meta[std::string(META_CONTENT_HASH_FLAG)] = *f->content_hash;
    if (encrypt) {
        if (f->encryption_iv.empty() || f->encrypted_with_key_version == 0)
            throw std::runtime_error("[CloudStorageEngine] Missing encryption metadata for upload: " + f->path.string());
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
    const auto backingPath = resolvedBackingPath(*this, f);
    if (!fs::exists(backingPath) || !fs::is_regular_file(backingPath))
        throw std::runtime_error("[CloudStorageEngine] Invalid file: " + f->path.string());

    if (!s3Vault()->encrypt_upstream) {
        const auto ciphertext = readFileToVector(backingPath);
        upload(f, ciphertext);
        return;
    }

    const fs::path s3Key = stripLeadingSlash(f->path);

    if (!f->content_hash) f->content_hash = db::query::fs::File::getContentHash(vault->id, f->path);
    const auto meta = getMetaMapFromFile(f);

    if (fs::file_size(backingPath) < s3::Controller::MIN_PART_SIZE)
        s3Provider_->uploadObjectWithMetadata(s3Key, backingPath, meta);
    else
        s3Provider_->uploadLargeObject(s3Key, backingPath, s3::Controller::MIN_PART_SIZE, meta);
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

std::vector<uint8_t> CloudEngine::decryptRemotePayload(
    const fs::path& rel_path,
    const std::vector<uint8_t>& payload,
    const std::shared_ptr<File>& remoteFile) const {
    const auto context = resolveRemoteEncryptionContext(rel_path, remoteFile);
    if (!context.encrypted) return payload;
    if (!context.payload)
        throw std::runtime_error("[CloudStorageEngine] No IV found for encrypted file: " + rel_path.string());

    const auto& [iv_b64, key_version] = *context.payload;
    return encryptionManager->decrypt(payload, iv_b64, key_version);
}

std::shared_ptr<File> CloudEngine::downloadFile(const fs::path& rel_path) {
    return downloadFileWithRemoteMetadata(rel_path, nullptr);
}

std::shared_ptr<File> CloudEngine::downloadFile(const std::shared_ptr<File>& remoteFile) {
    if (!remoteFile) throw std::invalid_argument("[CloudStorageEngine] Cannot download null remote file");
    return downloadFileWithRemoteMetadata(remoteFile->path, remoteFile);
}

std::shared_ptr<File> CloudEngine::downloadFileWithRemoteMetadata(
    const fs::path& rel_path,
    const std::shared_ptr<File>& remoteFile) {
    auto buffer = downloadToBuffer(rel_path);

    buffer = decryptRemotePayload(rel_path, buffer, remoteFile);

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
    indexed->remote_encrypted = remoteFile->remote_encrypted;
    if (!remoteFile->encryption_iv.empty())
        indexed->encryption_iv = remoteFile->encryption_iv;
    if (remoteFile->encrypted_with_key_version > 0)
        indexed->encrypted_with_key_version = remoteFile->encrypted_with_key_version;
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
        db::query::sync::RemoteObjectIndex::countForVault(vault->id) > 0) {
        db::query::sync::RemoteObjectIndex::upsertManifestETag(vault->id, manifestKey, remoteEtag);
        return true;
    }

    std::vector<uint8_t> buffer;
    s3Provider_->downloadToBuffer(manifestKey, buffer);
    const std::string manifest(buffer.begin(), buffer.end());
    const auto metadata = remote_manifest::inspectIndexV1Metadata(manifest, vault->id);
    const auto files = remote_manifest::parseIndexV1(manifest, vault->id);
    db::query::sync::RemoteObjectIndex::replaceFromManifest(vault->id, files);
    db::query::sync::RemoteObjectIndex::upsertManifestState(
        vault->id,
        manifestKey,
        remoteEtag,
        metadata.generated_at == 0 ? std::optional<std::time_t>{} : std::make_optional(metadata.generated_at),
        metadata.object_count,
        metadata.object_checksum);
    return true;
}

void CloudEngine::publishRemoteIndexManifest(const std::optional<std::string>& expectedETag) const {
    const auto files = db::query::sync::RemoteObjectIndex::listFilesForVault(vault->id);
    const auto manifest = remote_manifest::buildIndexV1(vault->id, files);
    const auto metadata = remote_manifest::inspectIndexV1Metadata(manifest, vault->id);
    const std::vector<uint8_t> payload(manifest.begin(), manifest.end());

    const auto ifMatch = expectedETag;
    const auto ifNoneMatch = ifMatch
        ? std::optional<std::string>{}
        : std::make_optional<std::string>("*");

    s3Provider_->uploadBufferWithMetadataConditional(
        remote_manifest::INDEX_V1_KEY,
        payload,
        {{"vh-manifest-version", std::to_string(remote_manifest::INDEX_V1_VERSION)}},
        ifMatch,
        ifNoneMatch);

    const auto head = s3Provider_->getHeadObject(remote_manifest::INDEX_V1_KEY);
    db::query::sync::RemoteObjectIndex::upsertManifestState(
        vault->id,
        remote_manifest::INDEX_V1_KEY,
        head ? header_value(*head, "ETag") : std::nullopt,
        metadata.generated_at == 0 ? std::optional<std::time_t>{} : std::make_optional(metadata.generated_at),
        metadata.object_count,
        metadata.object_checksum);
}

void CloudEngine::publishRemoteIndexManifestWithRetry() const {
    std::string lastConflict;
    auto expectedETag = db::query::sync::RemoteObjectIndex::getManifestETag(
        vault->id,
        remote_manifest::INDEX_V1_KEY);

    for (int attempt = 1; attempt <= REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS; ++attempt) {
        try {
            publishRemoteIndexManifest(expectedETag);
            return;
        } catch (const s3::ConditionalRequestFailed& e) {
            lastConflict = e.what();
            log::Registry::cloud()->warn(
                "[CloudStorageEngine] Remote index manifest publish conflict for vault {}; refreshing ETag before retry {}/{}",
                vault->id,
                attempt,
                REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS);

            const auto head = s3Provider_->getHeadObject(remote_manifest::INDEX_V1_KEY);
            expectedETag = head ? header_value(*head, "ETag") : std::nullopt;
            db::query::sync::RemoteObjectIndex::upsertManifestETag(
                vault->id,
                remote_manifest::INDEX_V1_KEY,
                expectedETag);
        }
    }

    throw std::runtime_error(
        "remote index manifest publish conflict after " +
        std::to_string(REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS) +
        " attempts" +
        (lastConflict.empty() ? std::string{} : ": " + lastConflict));
}

void CloudEngine::applyRemoteIndexMutation(const std::vector<Action>& plan) const {
    const auto replayMutation = [&]() {
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

        return mutated;
    };

    std::string lastConflict;
    auto expectedETag = db::query::sync::RemoteObjectIndex::getManifestETag(
        vault->id,
        remote_manifest::INDEX_V1_KEY);
    if (!expectedETag) {
        (void)refreshRemoteIndexFromManifestIfChanged();
        expectedETag = db::query::sync::RemoteObjectIndex::getManifestETag(
            vault->id,
            remote_manifest::INDEX_V1_KEY);
    }

    if (!replayMutation()) return;

    for (int attempt = 1; attempt <= REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS; ++attempt) {
        if (attempt > 1) {
            log::Registry::cloud()->warn(
                "[CloudStorageEngine] Remote index manifest publish conflict for vault {}; reloading manifest before retry {}/{}",
                vault->id,
                attempt,
                REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS);
            (void)refreshRemoteIndexFromManifestIfChanged();
            expectedETag = db::query::sync::RemoteObjectIndex::getManifestETag(
                vault->id,
                remote_manifest::INDEX_V1_KEY);
            replayMutation();
        }

        try {
            publishRemoteIndexManifest(expectedETag);
            return;
        } catch (const s3::ConditionalRequestFailed& e) {
            lastConflict = e.what();
        }
    }

    throw SyncStalled(
        "remote index manifest update conflict after " +
        std::to_string(REMOTE_MANIFEST_PUBLISH_MAX_ATTEMPTS) +
        " attempts" +
        (lastConflict.empty() ? std::string{} : ": " + lastConflict));
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
        if (const auto hash = header_value(*head, META_CONTENT_HASH)) return *hash;
    return "";
}

bool CloudEngine::remoteFileIsEncrypted(const fs::path& rel_path) const {
    return resolveRemoteEncryptionContext(rel_path).encrypted;
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
    const auto context = resolveRemoteEncryptionContext(rel_path);
    if (!context.payload) {
        log::Registry::cloud()->error("[CloudStorageEngine] No IV or key version found for encrypted file: {}", rel_path.string());
        return std::nullopt;
    }

    return context.payload;
}

CloudEngine::RemoteEncryptionContext CloudEngine::resolveRemoteEncryptionContext(
    const fs::path& rel_path,
    const std::shared_ptr<File>& remoteFile) const {
    RemoteEncryptionContext context;

    if (remoteFile) {
        context.encrypted = remoteFile->remote_encrypted.value_or(false);
        if (!remoteFile->encryption_iv.empty() && remoteFile->encrypted_with_key_version > 0) {
            context.encrypted = true;
            context.payload = std::make_pair(remoteFile->encryption_iv, remoteFile->encrypted_with_key_version);
            return context;
        }
    }

    if (const auto head = s3Provider_->getHeadObject(stripLeadingSlash(rel_path))) {
        if (const auto encrypted = header_value(*head, META_VH_ENCRYPTED)) {
            auto value = *encrypted;
            std::ranges::transform(value, value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            context.encrypted = context.encrypted || value == "true" || value == "1";
        }

        std::string iv_b64;
        unsigned int key_version = 0;
        if (const auto iv = header_value(*head, META_VH_IV)) iv_b64 = *iv;
        if (const auto version = header_value(*head, META_VH_KEY_VERSION))
            key_version = std::stoul(*version);

        if (!iv_b64.empty() && key_version > 0) {
            context.encrypted = true;
            context.payload = std::make_pair(iv_b64, key_version);
            return context;
        }
    }

    if (const auto localPayload = db::query::fs::File::getEncryptionIVAndVersion(vault->id, rel_path)) {
        context.encrypted = true;
        context.payload = localPayload;
    }

    return context;
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
