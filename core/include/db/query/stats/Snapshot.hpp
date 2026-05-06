#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model { struct StatsTrends; }

namespace vh::db::query::stats {

struct Snapshot {
    static void insertSystem(const std::string& snapshotType, const nlohmann::json& payload);
    static void insertVault(std::uint32_t vaultId, const std::string& snapshotType, const nlohmann::json& payload);
    static void purgeOlderThan(std::uint32_t retentionDays);

    static std::shared_ptr<::vh::stats::model::StatsTrends> systemTrends(std::uint32_t windowHours);
    static std::shared_ptr<::vh::stats::model::StatsTrends> vaultTrends(std::uint32_t vaultId, std::uint32_t windowHours);
};

}
