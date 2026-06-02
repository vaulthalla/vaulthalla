#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vh::identities { struct User; }

namespace vh::rbac::s3::policy {

enum class S3Action {
    ListBuckets,
    HeadBucket,
    CreateBucket,
    DeleteBucket,
    ListObjects,
    HeadObject,
    GetObject,
    PutObject,
    DeleteObject,
    DeleteObjects,
    CopyObjectSource,
    CopyObjectDestination,
    CreateMultipartUpload,
    UploadPart,
    CompleteMultipartUpload,
    AbortMultipartUpload,
    ListMultipartUploads,
    ListParts,
    ManageBucketBinding,
    ManageCredential,
    ManageBudget
};

struct S3PolicyRequest {
    std::shared_ptr<identities::User> principal;
    uint32_t credential_id{};
    std::string scope_mode{"user_access"};
    uint32_t vault_id{};
    std::filesystem::path vault_path{"/"};
    std::optional<std::filesystem::path> fuse_path;
    S3Action action{};
    bool object_exists{true};
    bool is_directory_marker{false};
    std::optional<uint32_t> target_user_id;
};

} // namespace vh::rbac::s3::policy
