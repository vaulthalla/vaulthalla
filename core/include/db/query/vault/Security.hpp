#pragma once

#include <cstdint>
#include <memory>

namespace vh::stats::model { struct VaultSecurity; }

namespace vh::db::query::vault {

struct Security {
    static std::shared_ptr<::vh::stats::model::VaultSecurity> getVaultSecurity(std::uint32_t vaultId);
};

}
