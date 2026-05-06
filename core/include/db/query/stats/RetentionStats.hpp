#pragma once

#include <cstdint>
#include <memory>

namespace vh::stats::model { struct RetentionStats; }

namespace vh::db::query::stats {

struct RetentionStats {
    static std::shared_ptr<::vh::stats::model::RetentionStats> snapshot();
    static std::shared_ptr<::vh::stats::model::RetentionStats> snapshotForVault(std::uint32_t vaultId);
};

}
