#include "storage/s3/pricing/PriceEstimate.hpp"

#include "config/Registry.hpp"
#include "log/Registry.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/s3/pricing/PriceBotUsage.hpp"
#include "storage/s3/pricing/PriceProfileResolver.hpp"
#include "sync/model/Action.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

namespace vh::storage::s3::pricing {
namespace {

constexpr const char* kStorageForecastUnknown =
    "storage-at-rest monthly forecast omitted; estimate covers planned requests/transfers only";

void appendIfMissing(std::vector<std::string>& values, const std::string& value) {
    if (std::ranges::find(values, value) == values.end()) values.push_back(value);
}

PriceEstimateReport unavailableFromTarget(
    const std::optional<PriceProfileTarget>& target,
    const std::string& reason) {
    if (!target) return PriceEstimateReport::unsupported(reason);
    return PriceEstimateReport::unavailable(*target, reason);
}

} // namespace

PriceEstimateReport estimatePlannedS3Sync(
    const vh::storage::CloudEngine& engine,
    const vh::sync::model::S3CostEstimate& s3Estimate,
    const PriceEstimateOptions options,
    IPriceBotClient* client) {
    const auto& cfg = config::Registry::get().pricing.storage_rates_api;
    if (options.disabled || !cfg.enabled)
        return PriceEstimateReport::unsupported("pricing disabled");

    const auto target = resolvePriceProfileTarget(
        engine.s3ProviderProfile(),
        engine.s3ApiKey(),
        engine.resolvedStorageTier());
    if (!target)
        return PriceEstimateReport::unsupported("S3 provider has no price-bot profile");

    PriceBotClient ownedClient(cfg);
    auto& priceClient = client ? *client : static_cast<IPriceBotClient&>(ownedClient);

    const auto profileResult = priceClient.getProfile(
        target->provider,
        target->region,
        target->storage_class,
        options.force_refresh);
    if (!profileResult.ok) {
        log::Registry::sync()->warn(
            "[PriceBot] Profile unavailable for {}: {}",
            target->profileId(),
            profileResult.error);
        return unavailableFromTarget(target, profileResult.error);
    }

    const auto usage = toPriceBotUsageInput(s3Estimate, engine.resolvedStorageTier());
    const auto estimateResult = priceClient.estimate(profileResult.value.raw, usage, options.force_refresh);
    if (!estimateResult.ok) {
        log::Registry::sync()->warn(
            "[PriceBot] Estimate unavailable for {}: {}",
            target->profileId(),
            estimateResult.error);
        return unavailableFromTarget(target, estimateResult.error);
    }

    PriceEstimateReport report;
    report.available = true;
    report.supported = true;
    report.stale = profileResult.stale || estimateResult.stale;
    report.target = *target;
    report.estimated_cost = estimateResult.value.estimated_cost;
    report.currency = estimateResult.value.currency;
    report.price_profile_id = profileResult.value.profile_id;
    report.catalog_version = profileResult.value.catalog_version;
    report.confidence_level = estimateResult.value.confidence_level.empty()
        ? profileResult.value.confidence_level
        : estimateResult.value.confidence_level;
    report.unknowns = estimateResult.value.unknowns;
    appendIfMissing(report.unknowns, kStorageForecastUnknown);
    report.breakdown = estimateResult.value.breakdown;
    return report;
}

std::string formatPriceEstimateForLog(const PriceEstimateReport& report) {
    if (!report.supported) return "pricing skipped: " + report.unavailable_reason;
    if (!report.available) return "pricing unavailable for " + report.target.profileId() + ": " + report.unavailable_reason;

    std::ostringstream out;
    out << "estimated_cost=" << report.estimated_cost << ' ' << report.currency
        << " profile=" << report.price_profile_id
        << " catalog=" << report.catalog_version
        << " confidence=" << report.confidence_level
        << " stale=" << (report.stale ? "true" : "false")
        << " unknowns=" << report.unknowns.size();
    return out.str();
}

std::string formatPriceEstimateForDryRun(const PriceEstimateReport& report) {
    std::ostringstream out;
    out << "  Price estimate:\n";
    if (!report.supported) {
        out << "    Status: skipped (" << report.unavailable_reason << ")";
        return out.str();
    }
    if (!report.available) {
        out << "    Status: unavailable (" << report.unavailable_reason << ")\n"
            << "    Profile: " << report.target.profileId();
        return out.str();
    }

    out << "    Estimated cost: " << report.estimated_cost << ' ' << report.currency << "\n"
        << "    Profile: " << report.price_profile_id << "\n"
        << "    Catalog: " << (report.catalog_version.empty() ? "unknown" : report.catalog_version) << "\n"
        << "    Confidence: " << (report.confidence_level.empty() ? "unknown" : report.confidence_level) << "\n"
        << "    Stale cache: " << (report.stale ? "yes" : "no") << "\n"
        << "    Unknowns: " << report.unknowns.size();
    if (!report.unknowns.empty()) out << " (" << report.unknowns.front() << ")";
    return out.str();
}

} // namespace vh::storage::s3::pricing
