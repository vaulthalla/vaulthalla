#pragma once

#include <memory>
#include "sync/Fwd.hpp"

namespace vh::db::query::sync {

class Policy {
    using PolicyPtr = std::shared_ptr<vh::sync::model::Policy>;

public:
    static void addSync(const PolicyPtr& sync);
    static void updateSync(const PolicyPtr& sync);
    static void deleteSync(unsigned int syncId);
    static void deleteSyncbyVaultId(unsigned int vaultId);
    static PolicyPtr getSync(unsigned int vaultId);
    static void reportSyncStarted(unsigned int syncId);
    static void reportSyncSuccess(unsigned int syncId);
};

}
