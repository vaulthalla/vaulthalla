#pragma once

#include "storage/s3/pricing/PriceCatalogStore.hpp"

namespace vh::storage::s3::pricing {

class PriceCatalogRefresher final {
public:
    [[nodiscard]] PriceCatalogRefreshResult refresh(PriceCatalogStore& store) const {
        return store.refresh();
    }
};

} // namespace vh::storage::s3::pricing
