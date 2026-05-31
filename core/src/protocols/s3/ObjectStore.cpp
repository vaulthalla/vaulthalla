#include "protocols/s3/ObjectStore.hpp"

#include "config/Registry.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "fs/Filesystem.hpp"
#include "fs/cache/Registry.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"
#include "protocols/s3/Error.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "runtime/Deps.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/Controller.hpp"
#include "sync/model/Action.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <openssl/md5.h>
#include <set>
#include <sstream>
#include <utility>

namespace vh::protocols::s3 {

namespace {
using Action = rbac::permission::vault::FilesystemAction;
using vh::fs::model::PathType;
using vh::fs::model::makeAbsolute;
using vh::fs::model::resolveParent;
using vh::fs::model::stripLeadingSlash;

std::string quoted(const std::string& value) {
    if (!value.empty() && value.front() == '"') return value;
    return "\"" + value + "\"";
}

std::string toHex(const unsigned char* bytes, const std::size_t n) {
    std::ostringstream out;
    for (std::size_t i = 0; i < n; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    return out.str();
}

std::vector<uint8_t> md5Raw(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> digest(MD5_DIGEST_LENGTH);
    MD5(bytes.data(), bytes.size(), digest.data());
    return digest;
}

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to read object backing file: " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string objectMd5FileHex(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to read object source file: " + path.string());

    MD5_CTX ctx{};
    MD5_Init(&ctx);
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) MD5_Update(&ctx, buffer.data(), static_cast<std::size_t>(count));
    }
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    return toHex(digest, MD5_DIGEST_LENGTH);
}

std::vector<uint8_t> readPlaintext(const std::shared_ptr<storage::Engine>& engine, const std::shared_ptr<fs::model::File>& file) {
    if (!file) return {};
    if (file->size_bytes == 0) return {};
    if (file->encryption_iv.empty() || file->encrypted_with_key_version == 0)
        return readFileBytes(file->backing_path);
    return engine->decrypt(file);
}

void removeIfExists(const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
        log::Registry::storage()->warn("[S3Gateway] Failed to remove {}: {}", path.string(), ec.message());
}

bool shouldMaterializeRemoteObject(
    const ResolvedBucket& bucket,
    const std::shared_ptr<storage::CloudEngine>& cloud) {
    if (bucket.mode == "remote_cache") return true;
    if (bucket.mode == "remote_proxy") return false;
    if (!cloud) return false;

    try {
        const auto policy = cloud->remote_policy();
        return policy && policy->strategy == sync::model::RemotePolicy::Strategy::Cache;
    } catch (const std::exception& e) {
        log::Registry::cloud()->warn(
            "[S3Gateway] Unable to inspect remote policy for vault {}: {}",
            bucket.vault_id,
            e.what());
        return false;
    }
}
}

std::vector<db::query::s3::BucketBinding> ObjectStore::listBuckets(const std::shared_ptr<identities::User>& actor) const {
    if (!actor) throw accessDenied("/");
    const auto bindings = db::query::s3::Gateway::listBuckets(std::nullopt);
    if (actor->isSuperAdmin()) return bindings;

    std::vector<db::query::s3::BucketBinding> visible;
    visible.reserve(bindings.size());

    const auto storageManager = runtime::Deps::get().storageManager;
    if (!storageManager) return visible;

    for (const auto& binding : bindings) {
        auto engine = storageManager->getEngine(binding.vault_id);
        if (!engine) continue;

        ResolvedBucket bucket{
            .bucket_name = binding.bucket_name,
            .vault_id = binding.vault_id,
            .mode = binding.mode,
            .api_exclusive = binding.api_exclusive,
            .engine = std::move(engine),
            .actor = actor
        };

        try {
            requirePermission(bucket, "/", Action::List);
            visible.push_back(binding);
        } catch (const S3Error& e) {
            if (e.code != "AccessDenied")
                log::Registry::runtime()->warn(
                    "[S3Gateway] Failed to evaluate bucket visibility for {}: {}",
                    binding.bucket_name,
                    e.what());
        } catch (const std::exception& e) {
            log::Registry::runtime()->warn(
                "[S3Gateway] Failed to evaluate bucket visibility for {}: {}",
                binding.bucket_name,
                e.what());
        }
    }

    return visible;
}

ResolvedBucket ObjectStore::resolveBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const {
    requireBucketName(bucket);
    if (!actor) throw accessDenied(bucket);

    const auto binding = db::query::s3::Gateway::resolveBucket(bucket);
    if (!binding) throw noSuchBucket(bucket);

    auto engine = runtime::Deps::get().storageManager->getEngine(binding->vault_id);
    if (!engine) throw noSuchBucket(bucket);

    ResolvedBucket out{
        .bucket_name = binding->bucket_name,
        .vault_id = binding->vault_id,
        .mode = binding->mode,
        .api_exclusive = binding->api_exclusive,
        .engine = std::move(engine),
        .actor = actor
    };
    return out;
}

ResolvedBucket ObjectStore::headBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const {
    auto out = resolveBucket(bucket, actor);
    requirePermission(out, "/", Action::List);
    return out;
}

ResolvedBucket ObjectStore::createBucket(
    const std::string& bucket,
    const std::shared_ptr<identities::User>& actor,
    const std::string& mode,
    const uintmax_t quotaBytes) const {
    requireBucketName(bucket);
    if (!actor) throw accessDenied(bucket);
    if (db::query::s3::Gateway::resolveBucket(bucket))
        return headBucket(bucket, actor);

    if (mode != "local")
        throw invalidArgument("Remote-backed buckets must be created with vh s3-gateway bucket create-remote-cache", bucket);

    auto vault = std::make_shared<vault::model::Vault>();
    vault->name = bucket;
    vault->description = "S3 gateway bucket " + bucket;
    vault->owner_id = actor->id;
    vault->type = vault::model::VaultType::Local;
    vault->quota = quotaBytes;
    vault->is_active = true;

    auto sync = std::make_shared<sync::model::LocalPolicy>();
    sync->conflict_policy = sync::model::LocalPolicy::ConflictPolicy::KeepBoth;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);
    db::query::s3::Gateway::bindBucket({
        .vault_id = vault->id,
        .bucket_name = bucket,
        .api_exclusive = config::Registry::get().s3_gateway.default_api_exclusive,
        .mode = "local",
        .created_by = actor->id
    });
    return headBucket(bucket, actor);
}

void ObjectStore::deleteBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const {
    auto resolved = headBucket(bucket, actor);
    requirePermission(resolved, "/", Action::Delete);
    const auto objects = db::query::s3::Gateway::listObjectStates(resolved.vault_id, {
        .prefix = {},
        .delimiter = std::nullopt,
        .start_after = std::nullopt,
        .continuation_token = std::nullopt,
        .max_keys = 1
    });
    if (!objects.objects.empty() || !objects.common_prefixes.empty())
        throw S3Error{"BucketNotEmpty", "The bucket you tried to delete is not empty", http::status::conflict, bucket};
    db::query::s3::Gateway::unbindBucket(bucket);
    if (resolved.api_exclusive)
        runtime::Deps::get().storageManager->removeVault(resolved.vault_id);
}

db::query::s3::ObjectListResult ObjectStore::listObjects(
    const ResolvedBucket& bucket,
    const db::query::s3::ObjectListParams& params) const {
    requirePermission(bucket, "/", Action::List);
    if (isRemoteBacked(bucket))
        db::query::s3::Gateway::backfillObjectStateFromRemoteIndex(bucket.vault_id);
    else
        backfillLocalObjectState(bucket);
    return db::query::s3::Gateway::listObjectStates(bucket.vault_id, params);
}

bool ObjectStore::remoteIndexStale(const ResolvedBucket& bucket) const {
    if (!isRemoteBacked(bucket)) return false;

    try {
        const auto cloud = cloudEngine(bucket);
        if (!cloud) return false;
        const auto policy = cloud->remote_policy();
        const auto summary = db::query::sync::RemoteObjectIndex::summaryForVault(bucket.vault_id);
        return summary.isStale(policy ? policy->max_remote_index_age : std::nullopt);
    } catch (const std::exception& e) {
        log::Registry::cloud()->warn(
            "[S3Gateway] Failed to evaluate remote index freshness for vault {}: {}",
            bucket.vault_id,
            e.what());
        return false;
    }
}

db::query::s3::ObjectState ObjectStore::headObject(const ResolvedBucket& bucket, const std::string& key) const {
    const auto vaultPath = keyToVaultPath(key);
    requirePermission(bucket, vaultPath, Action::Read);

    if (auto state = db::query::s3::Gateway::getObjectState(bucket.vault_id, vaultPathToKey(vaultPath)))
        return *state;
    if (auto state = stateFromLocalFile(bucket, vaultPath))
        return *state;

    if (isRemoteBacked(bucket)) {
        db::query::s3::Gateway::backfillObjectStateFromRemoteIndex(bucket.vault_id);
        if (auto state = db::query::s3::Gateway::getObjectState(bucket.vault_id, vaultPathToKey(vaultPath)))
            return *state;
    }

    throw noSuchKey(key);
}

ObjectBody ObjectStore::getObject(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::optional<ByteRange> range) const {
    const auto vaultPath = keyToVaultPath(key);
    auto state = headObject(bucket, key);
    auto metadata = db::query::s3::Gateway::listObjectMetadata(bucket.vault_id, vaultPathToKey(vaultPath));

    std::vector<uint8_t> bytes;
    if (isDirectoryMarker(key)) {
        bytes = {};
    } else if (const auto file = db::query::fs::File::getFileByPath(bucket.vault_id, vaultPath)) {
        bytes = readPlaintext(bucket.engine, file);
    } else if (const auto cloud = cloudEngine(bucket)) {
        auto remotePayload = cloud->downloadToBuffer(vaultPath);
        bytes = cloud->decryptRemotePayload(vaultPath, remotePayload);
        if (shouldMaterializeRemoteObject(bucket, cloud)) {
            try {
                ensureParentDirectories(bucket, vaultPath);
                fs::Filesystem::createFile({
                    .path = vaultPath,
                    .fuse_path = bucket.engine->vaultPathToFusePath(vaultPath),
                    .buffer = bytes,
                    .engine = bucket.engine,
                    .user = bucket.actor,
                    .overwrite = true
                });
            } catch (const std::exception& e) {
                log::Registry::cloud()->warn(
                    "[S3Gateway] Remote-cache materialization failed for vault {} key {}: {}",
                    bucket.vault_id,
                    vaultPath.generic_string(),
                    e.what());
            }
        }
    } else {
        throw noSuchKey(key);
    }

    std::optional<std::pair<uint64_t, uint64_t>> actualRange;
    if (range) {
        if (bytes.empty()) throw invalidRange(key);

        uint64_t first = 0;
        uint64_t last = bytes.size() - 1;
        if (range->first) {
            first = *range->first;
            if (range->last) last = std::min<uint64_t>(*range->last, bytes.size() - 1);
        } else if (range->last) {
            const auto suffixLength = *range->last;
            if (suffixLength == 0) throw invalidRange(key);
            first = suffixLength >= bytes.size() ? 0 : bytes.size() - suffixLength;
        }

        if (first >= bytes.size() || last < first) throw invalidRange(key);
        bytes = std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(first),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(last + 1));
        actualRange = std::make_pair(first, last);
    }

    return {
        .state = std::move(state),
        .metadata = std::move(metadata),
        .bytes = std::move(bytes),
        .content_range = actualRange
    };
}

db::query::s3::ObjectState ObjectStore::putObject(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::vector<uint8_t>& body,
    const PutObjectOptions& options) const {
    const auto vaultPath = keyToVaultPath(key);
    requirePermission(bucket, vaultPath, Action::Write);

    const auto etag = options.etag_override.value_or(quoted(md5Hex(body)));
    const auto objectKey = vaultPathToKey(vaultPath);
    const auto directoryMarker = isDirectoryMarker(key);

    if (directoryMarker && !body.empty())
        throw invalidArgument("Directory marker objects must be zero-byte objects", key);

    if (!directoryMarker) {
        ensureParentDirectories(bucket, vaultPath);
        const auto file = fs::Filesystem::createFile({
            .path = vaultPath,
            .fuse_path = bucket.engine->vaultPathToFusePath(vaultPath),
            .buffer = body,
            .engine = bucket.engine,
            .user = bucket.actor,
            .overwrite = true
        });

        if (isRemoteBacked(bucket)) {
            const auto cloud = cloudEngine(bucket);
            if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);
            cloud->upload(file);
            cloud->applyRemoteIndexMutation({
                sync::model::Action{
                    .type = sync::model::ActionType::Upload,
                    .key = {},
                    .local = file
                }
            });
        }
    } else if (isRemoteBacked(bucket)) {
        const auto cloud = cloudEngine(bucket);
        if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);
        const auto remoteFile = cloud->uploadBufferObject(vaultPath, {}, std::nullopt);
        cloud->applyRemoteIndexMutation({
            sync::model::Action{
                .type = sync::model::ActionType::Upload,
                .key = {},
                .local = remoteFile
            }
        });
    }

    db::query::s3::ObjectState state{
        .vault_id = bucket.vault_id,
        .object_key = objectKey,
        .etag = etag,
        .size_bytes = body.size(),
        .content_type = options.content_type.empty() ? std::make_optional<std::string>("application/octet-stream") : std::make_optional(options.content_type),
        .storage_class = options.storage_class,
        .last_modified = std::time(nullptr),
        .multipart = options.multipart,
        .part_count = options.part_count
    };
    db::query::s3::Gateway::upsertObject(state);
    db::query::s3::Gateway::upsertObjectMetadata(bucket.vault_id, objectKey, lowerMetadata(options.metadata));
    return *db::query::s3::Gateway::getObjectState(bucket.vault_id, objectKey);
}

db::query::s3::ObjectState ObjectStore::putObjectFromFile(
    const ResolvedBucket& bucket,
    const std::string& key,
    const std::filesystem::path& sourcePath,
    const uint64_t sourceSize,
    const PutObjectOptions& options) const {
    const auto vaultPath = keyToVaultPath(key);
    requirePermission(bucket, vaultPath, Action::Write);

    if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
        throw invalidArgument("Object source file is unavailable", sourcePath.string());
    const auto actualSize = std::filesystem::file_size(sourcePath);
    if (actualSize != sourceSize)
        throw std::runtime_error("S3 request body temp file size changed before commit");

    const auto directoryMarker = isDirectoryMarker(key);
    if (directoryMarker && sourceSize != 0)
        throw invalidArgument("Directory marker objects must be zero-byte objects", key);

    const auto etag = options.etag_override.value_or(quoted(objectMd5FileHex(sourcePath)));
    const auto objectKey = vaultPathToKey(vaultPath);

    if (!directoryMarker) {
        ensureParentDirectories(bucket, vaultPath);
        const auto file = fs::Filesystem::createFile({
            .path = vaultPath,
            .fuse_path = bucket.engine->vaultPathToFusePath(vaultPath),
            .buffer = {},
            .source_path = sourcePath,
            .engine = bucket.engine,
            .user = bucket.actor,
            .overwrite = true
        });

        if (isRemoteBacked(bucket)) {
            const auto cloud = cloudEngine(bucket);
            if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);
            cloud->upload(file);
            cloud->applyRemoteIndexMutation({
                sync::model::Action{
                    .type = sync::model::ActionType::Upload,
                    .key = {},
                    .local = file
                }
            });
        }
    } else if (isRemoteBacked(bucket)) {
        const auto cloud = cloudEngine(bucket);
        if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);
        const auto remoteFile = cloud->uploadBufferObject(vaultPath, {}, std::nullopt);
        cloud->applyRemoteIndexMutation({
            sync::model::Action{
                .type = sync::model::ActionType::Upload,
                .key = {},
                .local = remoteFile
            }
        });
    }

    db::query::s3::ObjectState state{
        .vault_id = bucket.vault_id,
        .object_key = objectKey,
        .etag = etag,
        .size_bytes = sourceSize,
        .content_type = options.content_type.empty() ? std::make_optional<std::string>("application/octet-stream") : std::make_optional(options.content_type),
        .storage_class = options.storage_class,
        .last_modified = std::time(nullptr),
        .multipart = options.multipart,
        .part_count = options.part_count
    };
    db::query::s3::Gateway::upsertObject(state);
    db::query::s3::Gateway::upsertObjectMetadata(bucket.vault_id, objectKey, lowerMetadata(options.metadata));
    return *db::query::s3::Gateway::getObjectState(bucket.vault_id, objectKey);
}

db::query::s3::ObjectState ObjectStore::copyObject(
    const ResolvedBucket& sourceBucket,
    const std::string& sourceKey,
    const ResolvedBucket& destBucket,
    const std::string& destKey,
    const PutObjectOptions& options) const {
    auto source = getObject(sourceBucket, sourceKey);
    auto putOptions = options;

    if (putOptions.metadata_directive == MetadataDirective::Copy) {
        putOptions.metadata = source.metadata;
        putOptions.content_type = source.state.content_type.value_or("application/octet-stream");
        if (!putOptions.storage_class) putOptions.storage_class = source.state.storage_class;
    } else if (!putOptions.content_type_explicit) {
        putOptions.content_type = "application/octet-stream";
    }

    return putObject(destBucket, destKey, source.bytes, putOptions);
}

void ObjectStore::deleteObject(const ResolvedBucket& bucket, const std::string& key) const {
    const auto vaultPath = keyToVaultPath(key);
    requirePermission(bucket, vaultPath, Action::Delete);
    const auto objectKey = vaultPathToKey(vaultPath);

    if (isRemoteBacked(bucket)) {
        const auto cloud = cloudEngine(bucket);
        if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);

        try {
            cloud->removeRemotely(vaultPath, true);
        } catch (const storage::s3::ObjectNotFound& e) {
            log::Registry::cloud()->warn(
                "[S3Gateway] Upstream object was already absent for vault {} key {}; continuing unversioned delete cleanup: {}",
                bucket.vault_id,
                objectKey,
                e.what());
        }

        try {
            db::query::s3::Gateway::deleteObjectStateAndRemoteIndex(bucket.vault_id, objectKey);
            purgeLocalObjectState(bucket.engine, vaultPath, bucket.actor->id);
            cloud->publishRemoteIndexManifestWithRetry();
        } catch (const std::exception& e) {
            log::Registry::cloud()->error(
                "[S3Gateway] Upstream delete succeeded but local cleanup failed for vault {} key {}: {}",
                bucket.vault_id,
                objectKey,
                e.what());
            throw;
        }
        return;
    }

    db::query::s3::Gateway::deleteObjectStateAndRemoteIndex(bucket.vault_id, objectKey);
    purgeLocalObjectState(bucket.engine, vaultPath, bucket.actor->id);
}

std::vector<std::pair<std::string, std::optional<std::string>>> ObjectStore::deleteObjects(
    const ResolvedBucket& bucket,
    const std::vector<std::string>& keys) const {
    std::vector<std::pair<std::string, std::optional<std::string>>> out;
    out.reserve(keys.size());
    for (const auto& key : keys) {
        try {
            deleteObject(bucket, key);
            out.emplace_back(key, std::nullopt);
        } catch (const S3Error& e) {
            out.emplace_back(key, e.code + ": " + e.what());
        } catch (const std::exception& e) {
            out.emplace_back(key, std::string("InternalError: ") + e.what());
        }
    }
    return out;
}

std::filesystem::path ObjectStore::keyToVaultPath(const std::string& key) {
    if (key.empty()) throw invalidArgument("Object key must not be empty");
    if (key.find('\0') != std::string::npos) throw invalidArgument("Object key must not contain NUL");

    for (const auto& part : std::filesystem::path(key)) {
        if (part == "..")
            throw invalidArgument("Object key escapes the bucket");
    }

    std::filesystem::path raw = std::filesystem::path("/") / key;
    const auto normalized = raw.lexically_normal();
    const auto normalizedStr = normalized.string();
    if (normalizedStr == "/" || normalizedStr.starts_with("/../") || normalizedStr.contains("/../") ||
        normalizedStr.ends_with("/.."))
        throw invalidArgument("Object key escapes the bucket");
    return makeAbsolute(normalized);
}

std::string ObjectStore::vaultPathToKey(const std::filesystem::path& path) {
    auto out = stripLeadingSlash(path).generic_string();
    if (out == "/") return {};
    return out;
}

std::string ObjectStore::md5Hex(const std::vector<uint8_t>& bytes) {
    const auto digest = md5Raw(bytes);
    return toHex(digest.data(), digest.size());
}

std::string ObjectStore::multipartEtag(const std::vector<std::vector<uint8_t>>& partMd5s) {
    std::vector<uint8_t> concat;
    concat.reserve(partMd5s.size() * MD5_DIGEST_LENGTH);
    for (const auto& md5 : partMd5s) concat.insert(concat.end(), md5.begin(), md5.end());
    return quoted(md5Hex(concat) + "-" + std::to_string(partMd5s.size()));
}

void ObjectStore::purgeLocalObjectState(
    const std::shared_ptr<storage::Engine>& engine,
    const std::filesystem::path& vaultPath,
    const uint32_t actorUserId) const {
    if (!engine) return;
    const auto fusePath = engine->vaultPathToFusePath(vaultPath);
    const auto file = db::query::fs::File::getFileByPath(engine->vault->id, vaultPath);
    std::optional<ino_t> oldInode;
    uint32_t oldId = 0;
    std::filesystem::path oldBacking;

    if (file) {
        oldInode = file->inode;
        oldId = file->id;
        oldBacking = file->backing_path;
        db::query::fs::File::deleteFile(actorUserId, file);
    }

    if (const auto& cache = runtime::Deps::get().fsCache) {
        cache->evictPath(fusePath);
        if (oldInode) cache->evictIno(*oldInode);
        if (oldId) cache->evictId(oldId);
    }

    if (file) {
        engine->purgeThumbnails(vaultPath);
        removeIfExists(oldBacking);
        removeIfExists(engine->paths->absPath(vaultPath, PathType::CACHE_ROOT));
        removeIfExists(engine->paths->absPath(vaultPath, PathType::FILE_CACHE_ROOT));
    }
}

void ObjectStore::requireObjectPermission(
    const ResolvedBucket& bucket,
    const std::string& key,
    const rbac::permission::vault::FilesystemAction action) const {
    requirePermission(bucket, keyToVaultPath(key), action);
}

void ObjectStore::requireBucketName(const std::string& bucket) {
    if (bucket.size() < 3 || bucket.size() > 63) throw invalidArgument("Invalid bucket name", bucket);
    if (bucket.find('/') != std::string::npos || bucket.find('\\') != std::string::npos)
        throw invalidArgument("Invalid bucket name", bucket);
}

void ObjectStore::requirePermission(
    const ResolvedBucket& bucket,
    const std::filesystem::path& vaultPath,
    const rbac::permission::vault::FilesystemAction action) {
    if (!bucket.actor) throw accessDenied(bucket.bucket_name);
    if (bucket.actor->isSuperAdmin()) return;
    const auto fusePath = bucket.engine->vaultPathToFusePath(makeAbsolute(vaultPath));
    if (!rbac::resolver::Vault::has<rbac::permission::vault::FilesystemAction>({
            .user = bucket.actor,
            .permission = action,
            .vault_id = bucket.vault_id,
            .path = fusePath
        }))
        throw accessDenied(fusePath.string());
}

bool ObjectStore::isRemoteBacked(const ResolvedBucket& bucket) {
    return bucket.mode == "remote_cache" || bucket.mode == "remote_proxy" ||
           (bucket.engine && bucket.engine->type() == storage::StorageType::Cloud);
}

bool ObjectStore::isDirectoryMarker(const std::string& key) {
    return !key.empty() && key.back() == '/';
}

std::shared_ptr<storage::CloudEngine> ObjectStore::cloudEngine(const ResolvedBucket& bucket) {
    return std::dynamic_pointer_cast<storage::CloudEngine>(bucket.engine);
}

std::map<std::string, std::string> ObjectStore::lowerMetadata(const std::map<std::string, std::string>& metadata) {
    std::map<std::string, std::string> out;
    for (const auto& item : metadata) {
        auto key = item.first;
        std::ranges::transform(key, key.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        out[std::move(key)] = item.second;
    }
    return out;
}

void ObjectStore::ensureParentDirectories(const ResolvedBucket& bucket, const std::filesystem::path& vaultPath) const {
    const auto parent = resolveParent(vaultPath);
    if (parent == "/" || parent.empty()) return;
    const auto err = fs::Filesystem::mkdir({
        .path = bucket.engine->vaultPathToFusePath(parent),
        .engine = bucket.engine,
        .user = bucket.actor
    });
    if (err && err != -EEXIST)
        throw std::runtime_error("Failed to create object parent directories");
}

void ObjectStore::backfillLocalObjectState(const ResolvedBucket& bucket) const {
    for (const auto& file : db::query::fs::File::listFilesInDir(bucket.vault_id, "/", true)) {
        if (!file) continue;
        const auto objectKey = vaultPathToKey(file->path);
        if (objectKey.empty()) continue;
        if (db::query::s3::Gateway::getObjectState(bucket.vault_id, objectKey)) continue;

        const auto plaintext = readPlaintext(bucket.engine, file);
        db::query::s3::Gateway::upsertObject({
            .vault_id = bucket.vault_id,
            .object_key = objectKey,
            .etag = quoted(md5Hex(plaintext)),
            .size_bytes = file->size_bytes,
            .content_type = file->mime_type,
            .storage_class = file->remote_storage_class,
            .last_modified = file->updated_at,
            .multipart = false,
            .part_count = std::nullopt
        });
    }
}

std::optional<db::query::s3::ObjectState> ObjectStore::stateFromLocalFile(
    const ResolvedBucket& bucket,
    const std::filesystem::path& vaultPath) const {
    const auto file = db::query::fs::File::getFileByPath(bucket.vault_id, vaultPath);
    if (!file) return std::nullopt;
    const auto plaintext = readPlaintext(bucket.engine, file);
    return db::query::s3::ObjectState{
        .vault_id = bucket.vault_id,
        .object_key = vaultPathToKey(vaultPath),
        .etag = quoted(md5Hex(plaintext)),
        .size_bytes = file->size_bytes,
        .content_type = file->mime_type,
        .storage_class = file->remote_storage_class,
        .last_modified = file->updated_at,
        .multipart = false,
        .part_count = std::nullopt
    };
}

} // namespace vh::protocols::s3
