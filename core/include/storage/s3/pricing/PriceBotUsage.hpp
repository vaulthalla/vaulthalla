#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"

#include <optional>

namespace vh::sync::model { struct S3CostEstimate; }

namespace vh::storage::s3::pricing {

[[nodiscard]] UsageInput toPriceBotUsageInput(
    const vh::sync::model::S3CostEstimate& estimate,
    const std::optional<provider::StorageTier>& tier);

} // namespace vh::storage::s3::pricing
