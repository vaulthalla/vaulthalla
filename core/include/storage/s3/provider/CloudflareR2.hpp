#pragma once

#include "storage/s3/provider/Provider.hpp"

namespace vh::storage::s3::provider {

class CloudflareR2Profile final : public Profile {
public:
    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::string displayName() const override;
    [[nodiscard]] SupportLevel supportLevel() const override;
    [[nodiscard]] std::vector<StorageTier> storageTiers() const override;
    [[nodiscard]] TierResolution normalizeStorageTier(
        const std::optional<std::string>& requested) const override;
    [[nodiscard]] RequestMutation requestMutation(
        RequestOperation operation,
        const std::optional<StorageTier>& vaultTier) const override;
    [[nodiscard]] std::optional<std::string> costProfileId() const override;
};

} // namespace vh::storage::s3::provider
