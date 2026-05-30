#include "storage/s3/provider/CloudflareR2.hpp"

#include <algorithm>
#include <utility>

namespace vh::storage::s3::provider {

RequestMutation storageClassMutation(RequestOperation operation, const std::optional<StorageTier>& vaultTier);

namespace {

StorageTier r2MakeTier(
    std::string id,
    std::string displayName,
    std::string wireClass,
    const bool retrievalFeePossible) {
    StorageTier tier;
    tier.id = std::move(id);
    tier.display_name = std::move(displayName);
    tier.wire_class = std::move(wireClass);
    tier.selectable = true;
    tier.immediate_read = true;
    tier.retrieval_fee_possible = retrievalFeePossible;
    return tier;
}

std::vector<StorageTier> r2Tiers() {
    return {
        r2MakeTier("standard", "R2 Standard", "STANDARD", false),
        r2MakeTier("infrequent_access", "R2 Infrequent Access", "STANDARD_IA", true)
    };
}

TierResolution r2OkUnset() {
    TierResolution resolution;
    resolution.ok = true;
    return resolution;
}

TierResolution r2ErrorResolution(std::string error) {
    TierResolution resolution;
    resolution.ok = false;
    resolution.error = std::move(error);
    return resolution;
}

TierResolution r2OkResolved(const StorageTier& tier) {
    TierResolution resolution;
    resolution.ok = true;
    resolution.resolved = tier;
    resolution.normalized_id = tier.id;
    return resolution;
}

} // namespace

std::string CloudflareR2Profile::id() const {
    return "cloudflare-r2";
}

std::string CloudflareR2Profile::displayName() const {
    return "Cloudflare R2";
}

SupportLevel CloudflareR2Profile::supportLevel() const {
    return SupportLevel::FirstClass;
}

std::vector<StorageTier> CloudflareR2Profile::storageTiers() const {
    return r2Tiers();
}

TierResolution CloudflareR2Profile::normalizeStorageTier(const std::optional<std::string>& requested) const {
    if (!requested) return r2OkUnset();
    if (containsControlCharacter(*requested))
        return r2ErrorResolution("invalid storage tier value: contains control characters");
    if (isProviderDefaultTierValue(*requested)) return r2OkUnset();

    const auto normalized = normalizeTierAliasKey(*requested);
    std::string tierId;
    if (normalized == "standard") tierId = "standard";
    else if (normalized == "standard_ia" || normalized == "infrequent_access") tierId = "infrequent_access";
    else return r2ErrorResolution(
        "storage tier '" + trimStorageTierValue(*requested) +
        "' is not selectable for Cloudflare R2 vault defaults yet");

    const auto tiers = r2Tiers();
    const auto it = std::ranges::find_if(tiers, [&](const StorageTier& tier) {
        return tier.id == tierId && tier.selectable;
    });
    if (it == tiers.end())
        return r2ErrorResolution("storage tier '" + tierId + "' is not selectable for Cloudflare R2 vault defaults yet");

    return r2OkResolved(*it);
}

RequestMutation CloudflareR2Profile::requestMutation(
    const RequestOperation operation,
    const std::optional<StorageTier>& vaultTier) const {
    return storageClassMutation(operation, vaultTier);
}

std::optional<std::string> CloudflareR2Profile::costProfileId() const {
    return "cloudflare-r2";
}

} // namespace vh::storage::s3::provider
