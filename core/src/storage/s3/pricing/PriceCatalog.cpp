#include "storage/s3/pricing/PriceCatalog.hpp"

namespace vh::storage::s3::pricing {

void PriceCatalog::clear() {
    profiles_.clear();
    catalog_version.clear();
    stale = false;
    source = kCatalogSourceDiskCache;
}

void PriceCatalog::put(RatingProfile profile) {
    if (catalog_version.empty()) catalog_version = profile.catalog_version;
    profiles_[profile.profile_id] = std::move(profile);
}

std::optional<RatingProfile> PriceCatalog::lookup(const PriceProfileTarget& target) const {
    const auto it = profiles_.find(target.profileId());
    if (it == profiles_.end()) return std::nullopt;
    return it->second;
}

} // namespace vh::storage::s3::pricing
