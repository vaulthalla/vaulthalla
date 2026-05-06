#pragma once

#include <cstdint>
#include <memory>
#include <optional>

namespace vh::stats::model { struct OperationStats; }

namespace vh::db::query::stats {

struct OperationStats {
    static std::shared_ptr<::vh::stats::model::OperationStats> snapshot();
    static std::shared_ptr<::vh::stats::model::OperationStats> snapshotForVault(std::uint32_t vaultId);
};

}
