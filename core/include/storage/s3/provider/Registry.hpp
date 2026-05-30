#pragma once

#include "storage/s3/provider/Provider.hpp"
#include "vault/model/APIKey.hpp"

namespace vh::storage::s3::provider {

[[nodiscard]] ProfilePtr resolve(vault::model::S3Provider provider);
[[nodiscard]] TierResolution normalizeStorageTier(
    vault::model::S3Provider provider,
    const std::optional<std::string>& requested);

} // namespace vh::storage::s3::provider
