#pragma once

#include "storage/s3/provider/StorageTier.hpp"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage::s3::pricing {

inline constexpr const char* kDefaultStorageRatesApiBaseUrl = "https://storage-rates-api.vaulthalla.cloud";

enum class PriceEstimateMode {
    Reporting,
    BudgetConservative
};

[[nodiscard]] std::string toString(PriceEstimateMode mode);

struct PriceProfileTarget {
    std::string provider;
    std::string region;
    std::string storage_class;

    [[nodiscard]] std::string profileId() const;
};

struct UsageInput {
    std::map<std::string, std::string> provider_operation_counts;
    std::map<std::string, std::string> storage_byte_hours;
    std::map<std::string, std::string> object_count_hours;
    std::string uploaded_bytes = "0";
    std::string downloaded_bytes = "0";
    std::string retrieval_bytes = "0";
    std::string retrieval_object_count = "0";
    std::string egress_bytes = "0";
    std::string object_count = "0";
    std::map<std::string, std::string> early_delete_gb_days;
};

struct RatingProfile {
    std::string profile_id;
    std::string catalog_version;
    std::string provider_id;
    std::string region;
    std::string storage_class;
    std::string confidence_level;
    nlohmann::json raw;

    [[nodiscard]] static RatingProfile parse(const nlohmann::json& payload);
};

struct EstimateResult {
    std::string estimated_cost;
    std::string currency;
    std::string estimate_mode;
    std::string free_tier_policy;
    std::optional<bool> free_tiers_applied;
    nlohmann::json breakdown;
    nlohmann::json free_tier_applied;
    nlohmann::json rounding_applied;
    std::string confidence_level;
    std::vector<std::string> unknowns;
    nlohmann::json raw;

    [[nodiscard]] static EstimateResult parse(const nlohmann::json& payload);
};

struct PriceEstimateReport {
    bool available{false};
    bool supported{false};
    bool stale{false};
    PriceProfileTarget target;
    std::string estimated_cost;
    std::string currency;
    std::string price_profile_id;
    std::string catalog_version;
    std::string confidence_level;
    std::string estimate_mode;
    std::string free_tier_policy;
    std::optional<bool> free_tiers_applied;
    std::vector<std::string> unknowns;
    nlohmann::json breakdown;
    std::string unavailable_reason;

    [[nodiscard]] static PriceEstimateReport unsupported(std::string reason);
    [[nodiscard]] static PriceEstimateReport unavailable(PriceProfileTarget target, std::string reason);
};

[[nodiscard]] std::string jsonDecimalToString(const nlohmann::json& payload);

void to_json(nlohmann::json& j, const UsageInput& usage);
void to_json(nlohmann::json& j, const PriceProfileTarget& target);
void to_json(nlohmann::json& j, const PriceEstimateReport& report);

} // namespace vh::storage::s3::pricing
