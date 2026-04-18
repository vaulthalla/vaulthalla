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
#include "fs/ops/file.hpp"
#include "preview/thumbnail/Worker.hpp"
#include "fs/Filesystem.hpp"
#include "runtime/Deps.hpp"
#include "vault/APIKeyManager.hpp"
#include "config/Registry.hpp"
#include "sync/model/RemotePolicy.hpp"

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

void CloudEngine::upload(const std::shared_ptr<File>& f) const {
    if (!fs::exists(f->backing_path) || !fs::is_regular_file(f->backing_path))
        throw std::runtime_error("[CloudStorageEngine] Invalid file: " + f->path.string());

    if (!s3Vault()->encrypt_upstream) {
        const auto ciphertext = readFileToVector(f->backing_path);
        upload(f, ciphertext);
        return;
    }

    const fs::path s3Key = stripLeadingSlash(f->path);

    if (fs::file_size(f->backing_path) < s3::Controller::MIN_PART_SIZE) s3Provider_->uploadObject(s3Key, f->backing_path);
    else s3Provider_->uploadLargeObject(s3Key, f->backing_path);

    std::vector<uint8_t> buffer;
    s3Provider_->downloadToBuffer(s3Key, buffer);

    if (!f->content_hash) f->content_hash = db::query::fs::File::getContentHash(vault->id, f->path);
    s3Provider_->setObjectContentHash(s3Key, *f->content_hash);
    s3Provider_->setObjectEncryptionMetadata(s3Key, f->encryption_iv, f->encrypted_with_key_version);
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
    else s3Provider_->uploadLargeObject(s3Key, buffer);
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

    const auto f = Filesystem::createFile({
            .path = makeAbsolute(rel_path),
            .fuse_path = paths->absRelToAbsRel(makeAbsolute(rel_path), PathType::VAULT_ROOT, PathType::FUSE_ROOT),
            .buffer = buffer,
            .engine = shared_from_this(),
            .userId = vault->owner_id,
            .overwrite = true
        });

    s3Provider_->uploadBufferWithMetadata(s3Key, buffer, getMetaMapFromFile(f));

    preview::thumbnail::Worker::enqueue(shared_from_this(), buffer, f);

    return f;
}

void CloudEngine::indexAndDeleteFile(const fs::path& rel_path) {
    const auto index = downloadFile(rel_path);
    fs::remove(paths->absPath(index->path, PathType::FILE_CACHE_ROOT));
}

std::unordered_map<std::u8string, std::shared_ptr<File>>
CloudEngine::getGroupedFilesFromS3(const fs::path& prefix) const {
    return groupEntriesByPath(filesFromS3XML(s3Provider_->listObjects(prefix)));
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
    const auto vaultPath = paths->absRelToAbsRel(f->path, PathType::FUSE_ROOT, PathType::VAULT_ROOT);
    s3Provider_->deleteObject(stripLeadingSlash(vaultPath));
    if (rmThumbnails) purgeThumbnails(vaultPath);
}

std::shared_ptr<S3Vault> CloudEngine::s3Vault() const { return std::static_pointer_cast<S3Vault>(vault); }

std::shared_ptr<RemotePolicy> CloudEngine::remote_policy() const {
    return std::static_pointer_cast<RemotePolicy>(sync);
}
