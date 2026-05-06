#pragma once

#include <memory>

namespace vh::stats::model { struct VaultSyncHealth; }

namespace vh::db::query::sync {

class Stats {
public:
    static std::shared_ptr<vh::stats::model::VaultSyncHealth> getVaultSyncHealth(unsigned int vaultId);
};

}
