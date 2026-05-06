#pragma once

#include <cstdint>
#include <memory>

namespace vh::stats::model { struct VaultActivity; }

namespace vh::db::query::vault {

class Activity {
public:
    static std::shared_ptr<::vh::stats::model::VaultActivity> getVaultActivity(std::uint32_t vaultId);
};

}
