#include "storage/s3/provider/Aws.hpp"

#include <algorithm>
#include <utility>

namespace vh::storage::s3::provider {

RequestMutation storageClassMutation(RequestOperation operation, const std::optional<StorageTier>& vaultTier);

namespace {

StorageTier awsMakeTier(
    std::string id,
    std::string displayName,
    std::string wireClass,
    const bool retrievalFeePossible,
    const std::optional<unsigned int> minimumDuration = std::nullopt,
    const std::optional<std::uint64_t> minimumSize = std::nullopt) {
    StorageTier tier;
    tier.id = std::move(id);
    tier.display_name = std::move(displayName);
    tier.wire_class = std::move(wireClass);
    tier.selectable = true;
    tier.immediate_read = true;
    tier.retrieval_fee_possible = retrievalFeePossible;
    tier.minimum_storage_duration_days = minimumDuration;
    tier.minimum_billable_object_size_bytes = minimumSize;
    return tier;
}

std::vector<StorageTier> awsTiers() {
    return {
        awsMakeTier("standard", "S3 Standard", "STANDARD", false),
        awsMakeTier("standard_ia", "S3 Standard-IA", "STANDARD_IA", true, 30, 128ull * 1024ull)
    };
}

TierResolution awsOkUnset() {
    TierResolution resolution;
    resolution.ok = true;
    return resolution;
}

TierResolution awsErrorResolution(std::string error) {
    TierResolution resolution;
    resolution.ok = false;
    resolution.error = std::move(error);
    return resolution;
}

TierResolution awsOkResolved(const StorageTier& tier) {
    TierResolution resolution;
    resolution.ok = true;
    resolution.resolved = tier;
    resolution.normalized_id = tier.id;
    return resolution;
}

} // namespace

std::string AwsProfile::id() const {
    return "aws-s3";
}

std::string AwsProfile::displayName() const {
    return "AWS S3";
}

SupportLevel AwsProfile::supportLevel() const {
    return SupportLevel::FirstClass;
}

std::vector<StorageTier> AwsProfile::storageTiers() const {
    return awsTiers();
}

TierResolution AwsProfile::normalizeStorageTier(const std::optional<std::string>& requested) const {
    if (!requested) return awsOkUnset();
    if (containsControlCharacter(*requested))
        return awsErrorResolution("invalid storage tier value: contains control characters");
    if (isProviderDefaultTierValue(*requested)) return awsOkUnset();

    const auto normalized = normalizeTierAliasKey(*requested);
    std::string tierId;
    if (normalized == "standard") tierId = "standard";
    else if (normalized == "standard_ia") tierId = "standard_ia";
    else return awsErrorResolution(
        "storage tier '" + trimStorageTierValue(*requested) +
        "' is not selectable for AWS S3 vault defaults yet");

    const auto tiers = awsTiers();
    const auto it = std::ranges::find_if(tiers, [&](const StorageTier& tier) {
        return tier.id == tierId && tier.selectable;
    });
    if (it == tiers.end())
        return awsErrorResolution("storage tier '" + tierId + "' is not selectable for AWS S3 vault defaults yet");

    return awsOkResolved(*it);
}

RequestMutation AwsProfile::requestMutation(
    const RequestOperation operation,
    const std::optional<StorageTier>& vaultTier) const {
    return storageClassMutation(operation, vaultTier);
}

std::optional<std::string> AwsProfile::costProfileId() const {
    return "aws-s3";
}

} // namespace vh::storage::s3::provider
