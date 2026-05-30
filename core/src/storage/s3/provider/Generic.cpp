#include "storage/s3/provider/Generic.hpp"

#include <utility>

namespace vh::storage::s3::provider {
namespace {

TierResolution genericOkUnset() {
    TierResolution resolution;
    resolution.ok = true;
    return resolution;
}

TierResolution genericErrorResolution(std::string error) {
    TierResolution resolution;
    resolution.ok = false;
    resolution.error = std::move(error);
    return resolution;
}

} // namespace

GenericProfile::GenericProfile(std::string providerDisplayName)
    : providerDisplayName_(std::move(providerDisplayName)) {}

std::string GenericProfile::id() const {
    return "generic-s3-compatible";
}

std::string GenericProfile::displayName() const {
    return providerDisplayName_;
}

SupportLevel GenericProfile::supportLevel() const {
    return SupportLevel::Generic;
}

std::vector<StorageTier> GenericProfile::storageTiers() const {
    return {};
}

TierResolution GenericProfile::normalizeStorageTier(const std::optional<std::string>& requested) const {
    if (!requested) return genericOkUnset();
    if (containsControlCharacter(*requested))
        return genericErrorResolution("invalid storage tier value: contains control characters");
    if (isProviderDefaultTierValue(*requested)) return genericOkUnset();

    return genericErrorResolution(
        "storage tier selection requires a first-class S3 provider profile; provider " +
        displayName() + " is running in generic S3-compatible mode");
}

RequestMutation GenericProfile::requestMutation(
    RequestOperation,
    const std::optional<StorageTier>&) const {
    return {};
}

std::optional<std::string> GenericProfile::costProfileId() const {
    return std::nullopt;
}

} // namespace vh::storage::s3::provider
