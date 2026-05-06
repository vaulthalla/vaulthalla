#pragma once

#include <cstdint>
#include <memory>

namespace vh::stats::model { struct VaultRecovery; }

namespace vh::db::query::vault {

struct Recovery {
    static std::shared_ptr<::vh::stats::model::VaultRecovery> getVaultRecovery(std::uint32_t vaultId);
};

}
