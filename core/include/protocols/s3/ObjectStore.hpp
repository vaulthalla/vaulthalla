#pragma once

#include "db/query/s3/Gateway.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::identities { struct User; }
namespace vh::storage { struct Engine; class CloudEngine; }
namespace vh::fs::model { struct File; }

namespace vh::protocols::s3 {

struct ResolvedBucket {
    std::string bucket_name;
    uint32_t vault_id{};
    std::string mode;
    bool api_exclusive{};
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<identities::User> actor;
};

enum class MetadataDirective {
    Copy,
    Replace
};

struct PutObjectOptions {
    std::string content_type{"application/octet-stream"};
    bool content_type_explicit{};
    std::optional<std::string> storage_class;
    std::map<std::string, std::string> metadata;
    MetadataDirective metadata_directive{MetadataDirective::Replace};
    bool multipart{};
    std::optional<uint32_t> part_count;
    std::optional<std::string> etag_override;
};

struct ByteRange {
    std::optional<uint64_t> first;
    std::optional<uint64_t> last;
};

struct ObjectBody {
    db::query::s3::ObjectState state;
    std::map<std::string, std::string> metadata;
    std::vector<uint8_t> bytes;
    std::optional<std::pair<uint64_t, uint64_t>> content_range;
};

class ObjectStore {
public:
    std::vector<db::query::s3::BucketBinding> listBuckets(const std::shared_ptr<identities::User>& actor) const;
    ResolvedBucket resolveBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const;
    ResolvedBucket headBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const;
    ResolvedBucket createBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor,
                                const std::string& mode = "local",
                                uintmax_t quotaBytes = 0) const;
    void deleteBucket(const std::string& bucket, const std::shared_ptr<identities::User>& actor) const;

    db::query::s3::ObjectListResult listObjects(const ResolvedBucket& bucket,
                                                const db::query::s3::ObjectListParams& params) const;
    bool remoteIndexStale(const ResolvedBucket& bucket) const;
    db::query::s3::ObjectState headObject(const ResolvedBucket& bucket, const std::string& key) const;
    ObjectBody getObject(const ResolvedBucket& bucket, const std::string& key,
                         std::optional<ByteRange> range = std::nullopt) const;
    db::query::s3::ObjectState putObject(const ResolvedBucket& bucket, const std::string& key,
                                         const std::vector<uint8_t>& body,
                                         const PutObjectOptions& options = {}) const;
    db::query::s3::ObjectState putObjectFromFile(const ResolvedBucket& bucket, const std::string& key,
                                                 const std::filesystem::path& sourcePath,
                                                 uint64_t sourceSize,
                                                 const PutObjectOptions& options = {}) const;
    db::query::s3::ObjectState copyObject(const ResolvedBucket& sourceBucket, const std::string& sourceKey,
                                          const ResolvedBucket& destBucket, const std::string& destKey,
                                          const PutObjectOptions& options = {}) const;
    void deleteObject(const ResolvedBucket& bucket, const std::string& key) const;
    std::vector<std::pair<std::string, std::optional<std::string>>> deleteObjects(
        const ResolvedBucket& bucket,
        const std::vector<std::string>& keys) const;

    static std::filesystem::path keyToVaultPath(const std::string& key);
    static std::string vaultPathToKey(const std::filesystem::path& path);
    static std::string md5Hex(const std::vector<uint8_t>& bytes);
    static std::string multipartEtag(const std::vector<std::vector<uint8_t>>& partMd5s);

    void purgeLocalObjectState(const std::shared_ptr<storage::Engine>& engine,
                               const std::filesystem::path& vaultPath,
                               uint32_t actorUserId) const;
    void requireObjectPermission(const ResolvedBucket& bucket,
                                 const std::string& key,
                                 rbac::permission::vault::FilesystemAction action) const;

private:
    static void requireBucketName(const std::string& bucket);
    static void requirePermission(const ResolvedBucket& bucket, const std::filesystem::path& vaultPath,
                                  rbac::permission::vault::FilesystemAction action);
    static bool isRemoteBacked(const ResolvedBucket& bucket);
    static bool isDirectoryMarker(const std::string& key);
    static std::shared_ptr<storage::CloudEngine> cloudEngine(const ResolvedBucket& bucket);
    static std::map<std::string, std::string> lowerMetadata(const std::map<std::string, std::string>& metadata);
    void ensureParentDirectories(const ResolvedBucket& bucket, const std::filesystem::path& vaultPath) const;
    void backfillLocalObjectState(const ResolvedBucket& bucket) const;
    std::optional<db::query::s3::ObjectState> stateFromLocalFile(const ResolvedBucket& bucket,
                                                                 const std::filesystem::path& vaultPath) const;
};

}
