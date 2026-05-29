#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"
#include "storage/s3/provider/Provider.hpp"

#include <memory>
#include <optional>
#include <string>

namespace vh::vault::model { struct APIKey; }

namespace vh::storage::s3::pricing {

[[nodiscard]] std::optional<std::string> priceBotStorageClassId(
    const provider::Profile& profile,
    const std::optional<provider::StorageTier>& tier);

[[nodiscard]] std::optional<PriceProfileTarget> resolvePriceProfileTarget(
    const provider::ProfilePtr& profile,
    const std::shared_ptr<vault::model::APIKey>& apiKey,
    const std::optional<provider::StorageTier>& tier);

} // namespace vh::storage::s3::pricing
