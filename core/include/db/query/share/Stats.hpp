#pragma once

#include <cstdint>
#include <memory>

namespace vh::stats::model { struct VaultShareStats; }

namespace vh::db::query::share {

struct Stats {
    static std::shared_ptr<::vh::stats::model::VaultShareStats> getVaultShareStats(std::uint32_t vaultId);
};

}
