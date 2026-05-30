#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"

namespace vh::storage::s3::pricing {

struct LocalEstimateOptions {
    PriceEstimateMode mode{PriceEstimateMode::Reporting};
    bool apply_free_tiers{true};
};

class LocalEstimator final {
public:
    [[nodiscard]] EstimateResult estimate(
        const RatingProfile& profile,
        const UsageInput& usage,
        LocalEstimateOptions options = {}) const;
};

} // namespace vh::storage::s3::pricing
