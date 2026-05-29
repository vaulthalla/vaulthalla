#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage::s3::pricing {

inline constexpr const char* kCatalogSourceApi = "api";
inline constexpr const char* kCatalogSourceFallback = "fallback";
inline constexpr const char* kCatalogSourceDiskCache = "disk-cache";

struct CatalogProfile {
    RatingProfile profile;
    bool stale{false};
    std::string source{kCatalogSourceDiskCache};
};

class PriceCatalog final {
public:
    void clear();
    void put(RatingProfile profile);

    [[nodiscard]] std::optional<RatingProfile> lookup(const PriceProfileTarget& target) const;
    [[nodiscard]] bool empty() const noexcept { return profiles_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return profiles_.size(); }

    std::string catalog_version;
    bool stale{false};
    std::string source{kCatalogSourceDiskCache};

private:
    std::map<std::string, RatingProfile> profiles_;
};

struct PriceCatalogRefreshResult {
    bool ok{false};
    bool stale{false};
    std::string source;
    std::string error;
    PriceCatalog catalog;
};

} // namespace vh::storage::s3::pricing
