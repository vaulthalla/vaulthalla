#pragma once

#include "storage/s3/pricing/PriceBotClient.hpp"
#include "storage/s3/pricing/PriceBotModels.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace vh::storage { class CloudEngine; }
namespace vh::sync::model { struct S3CostEstimate; }

namespace vh::storage::s3::pricing {

class IPriceCatalogStore;

struct PriceEstimateOptions {
    bool force_refresh{false};
    bool disabled{false};
    PriceEstimateMode mode{PriceEstimateMode::Reporting};
};

[[nodiscard]] PriceEstimateReport estimatePlannedS3Sync(
    const vh::storage::CloudEngine& engine,
    const vh::sync::model::S3CostEstimate& s3Estimate,
    PriceEstimateOptions options = {},
    IPriceBotClient* client = nullptr,
    IPriceCatalogStore* catalogStore = nullptr);

[[nodiscard]] std::string formatPriceEstimateForLog(const PriceEstimateReport& report);
[[nodiscard]] std::string formatPriceEstimateForDryRun(
    const PriceEstimateReport& report,
    std::string_view label = "Price estimate");

} // namespace vh::storage::s3::pricing
