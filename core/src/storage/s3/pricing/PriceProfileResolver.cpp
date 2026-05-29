#include "storage/s3/pricing/PriceProfileResolver.hpp"

#include "vault/model/APIKey.hpp"

namespace vh::storage::s3::pricing {

std::optional<std::string> priceBotStorageClassId(
    const provider::Profile& profile,
    const std::optional<provider::StorageTier>& tier) {
    const auto provider = profile.costProfileId();
    if (!provider) return std::nullopt;

    const auto tierId = tier ? tier->id : std::string{"standard"};
    if (*provider == "aws-s3") {
        if (tierId == "standard" || tierId.empty()) return "standard";
        if (tierId == "standard_ia") return "standard-ia";
        return std::nullopt;
    }

    if (*provider == "cloudflare-r2") {
        if (tierId == "standard" || tierId.empty()) return "standard";
        if (tierId == "infrequent_access") return "infrequent-access";
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<PriceProfileTarget> resolvePriceProfileTarget(
    const provider::ProfilePtr& profile,
    const std::shared_ptr<vault::model::APIKey>& apiKey,
    const std::optional<provider::StorageTier>& tier) {
    if (!profile) return std::nullopt;
    const auto provider = profile->costProfileId();
    if (!provider) return std::nullopt;

    const auto storageClass = priceBotStorageClassId(*profile, tier);
    if (!storageClass) return std::nullopt;

    PriceProfileTarget target;
    target.provider = *provider;
    target.storage_class = *storageClass;

    if (*provider == "cloudflare-r2") target.region = "global";
    else {
        if (!apiKey || apiKey->region.empty()) return std::nullopt;
        target.region = apiKey->region;
    }

    return target;
}

} // namespace vh::storage::s3::pricing
