#include "storage/s3/pricing/PriceBotModels.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace vh::storage::s3::pricing {
namespace {

std::string optionalStringField(const nlohmann::json& payload, const char* key) {
    const auto it = payload.find(key);
    if (it == payload.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

} // namespace

std::string toString(const PriceEstimateMode mode) {
    switch (mode) {
        case PriceEstimateMode::BudgetConservative:
            return "budget_conservative";
        case PriceEstimateMode::Reporting:
        default:
            return "reporting";
    }
}

std::string PriceProfileTarget::profileId() const {
    return provider + "/" + region + "/" + storage_class;
}

std::string jsonDecimalToString(const nlohmann::json& payload) {
    if (payload.is_string()) return payload.get<std::string>();
    if (payload.is_number_integer()) return std::to_string(payload.get<long long>());
    if (payload.is_number_unsigned()) return std::to_string(payload.get<unsigned long long>());
    if (payload.is_number_float()) return payload.dump();
    if (payload.is_null()) return {};
    return payload.dump();
}

RatingProfile RatingProfile::parse(const nlohmann::json& payload) {
    RatingProfile profile;
    profile.raw = payload;
    profile.profile_id = payload.value("profile_id", "");
    profile.catalog_version = payload.value("catalog_version", "");

    if (payload.contains("provider") && payload.at("provider").is_object())
        profile.provider_id = payload.at("provider").value("id", "");

    if (payload.contains("scope") && payload.at("scope").is_object()) {
        const auto& scope = payload.at("scope");
        profile.region = scope.value("region", "");
        profile.storage_class = scope.value("storage_class", "");
    }

    if (payload.contains("confidence") && payload.at("confidence").is_object())
        profile.confidence_level = payload.at("confidence").value("level", "");

    if (profile.profile_id.empty() || profile.provider_id.empty() ||
        profile.region.empty() || profile.storage_class.empty())
        throw std::invalid_argument("price-bot rating profile is missing required identity fields");

    return profile;
}

EstimateResult EstimateResult::parse(const nlohmann::json& payload) {
    EstimateResult result;
    result.raw = payload;
    if (!payload.contains("estimated_cost"))
        throw std::invalid_argument("price-bot estimate is missing estimated_cost");
    result.estimated_cost = jsonDecimalToString(payload.at("estimated_cost"));
    result.currency = payload.value("currency", "USD");
    result.estimate_mode = optionalStringField(payload, "estimate_mode");
    result.free_tier_policy = optionalStringField(payload, "free_tier_policy");
    if (payload.contains("free_tiers_applied") && payload.at("free_tiers_applied").is_boolean())
        result.free_tiers_applied = payload.at("free_tiers_applied").get<bool>();
    result.breakdown = payload.value("breakdown", nlohmann::json::array());
    result.free_tier_applied = payload.value("free_tier_applied", nlohmann::json::object());
    result.rounding_applied = payload.value("rounding_applied", nlohmann::json::object());

    if (payload.contains("confidence") && payload.at("confidence").is_object())
        result.confidence_level = payload.at("confidence").value("level", "");

    if (payload.contains("unknowns") && payload.at("unknowns").is_array()) {
        for (const auto& item : payload.at("unknowns"))
            if (item.is_string()) result.unknowns.push_back(item.get<std::string>());
    }

    return result;
}

PriceEstimateReport PriceEstimateReport::unsupported(std::string reason) {
    PriceEstimateReport report;
    report.available = false;
    report.supported = false;
    report.unavailable_reason = std::move(reason);
    return report;
}

PriceEstimateReport PriceEstimateReport::unavailable(PriceProfileTarget target, std::string reason) {
    PriceEstimateReport report;
    report.available = false;
    report.supported = true;
    report.target = std::move(target);
    report.unavailable_reason = std::move(reason);
    return report;
}

void to_json(nlohmann::json& j, const UsageInput& usage) {
    j = {
        {"provider_operation_counts", usage.provider_operation_counts},
        {"storage_byte_hours", usage.storage_byte_hours},
        {"object_count_hours", usage.object_count_hours},
        {"uploaded_bytes", usage.uploaded_bytes},
        {"downloaded_bytes", usage.downloaded_bytes},
        {"retrieval_bytes", usage.retrieval_bytes},
        {"retrieval_object_count", usage.retrieval_object_count},
        {"egress_bytes", usage.egress_bytes},
        {"object_count", usage.object_count},
        {"early_delete_gb_days", usage.early_delete_gb_days}
    };
}

void to_json(nlohmann::json& j, const PriceProfileTarget& target) {
    j = {
        {"provider", target.provider},
        {"region", target.region},
        {"storage_class", target.storage_class},
        {"profile_id", target.profileId()}
    };
}

void to_json(nlohmann::json& j, const PriceEstimateReport& report) {
    j = {
        {"available", report.available},
        {"supported", report.supported},
        {"stale", report.stale},
        {"target", report.target},
        {"estimated_cost", report.available ? nlohmann::json(report.estimated_cost) : nlohmann::json(nullptr)},
        {"currency", report.available ? nlohmann::json(report.currency) : nlohmann::json(nullptr)},
        {"price_profile_id", report.price_profile_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.price_profile_id)},
        {"catalog_version", report.catalog_version.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.catalog_version)},
        {"catalog_source", report.catalog_source.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.catalog_source)},
        {"confidence_level", report.confidence_level.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.confidence_level)},
        {"estimate_mode", report.estimate_mode.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.estimate_mode)},
        {"free_tier_policy", report.free_tier_policy.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.free_tier_policy)},
        {"free_tiers_applied", report.free_tiers_applied ? nlohmann::json(*report.free_tiers_applied) : nlohmann::json(nullptr)},
        {"unknowns", report.unknowns},
        {"breakdown", report.breakdown.is_null() ? nlohmann::json::array() : report.breakdown},
        {"unavailable_reason", report.unavailable_reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(report.unavailable_reason)}
    };
}

} // namespace vh::storage::s3::pricing
