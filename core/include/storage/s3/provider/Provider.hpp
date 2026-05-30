#pragma once

#include "storage/s3/provider/StorageTier.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage::s3::provider {

enum class SupportLevel {
    Generic,
    FirstClass
};

enum class RequestOperation {
    PutObject,
    CreateMultipartUpload,
    CopyObjectRewrite,
    UploadPart,
    CompleteMultipartUpload,
    HeadObject,
    GetObject,
    DeleteObject,
    ListObjects
};

struct RequestMutation {
    std::map<std::string, std::string> system_headers;
};

class Profile {
public:
    virtual ~Profile() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual SupportLevel supportLevel() const = 0;
    [[nodiscard]] virtual std::vector<StorageTier> storageTiers() const = 0;
    [[nodiscard]] virtual TierResolution normalizeStorageTier(
        const std::optional<std::string>& requested) const = 0;
    [[nodiscard]] virtual RequestMutation requestMutation(
        RequestOperation operation,
        const std::optional<StorageTier>& vaultTier) const = 0;
    [[nodiscard]] virtual std::optional<std::string> costProfileId() const = 0;
};

using ProfilePtr = std::shared_ptr<const Profile>;

} // namespace vh::storage::s3::provider
