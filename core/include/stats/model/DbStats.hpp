#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct DbTableStats {
    std::string tableName;
    std::uint64_t totalBytes = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t indexBytes = 0;
    std::uint64_t rowEstimate = 0;
};

struct DbStats {
    bool connected = false;
    std::string status = "unknown";
    std::optional<std::string> error;

    std::optional<std::string> databaseName;
    std::uint64_t dbSizeBytes = 0;

    std::uint64_t connectionsActive = 0;
    std::uint64_t connectionsIdle = 0;
    std::uint64_t connectionsIdleInTransaction = 0;
    std::uint64_t connectionsTotal = 0;
    std::optional<std::uint64_t> connectionsMax;

    std::optional<double> cacheHitRatio;
    bool pgStatStatementsEnabled = false;
    std::optional<std::uint64_t> slowQueryCount;
    std::uint64_t deadlocks = 0;
    std::uint64_t tempBytes = 0;
    std::optional<std::uint64_t> oldestTransactionAgeSeconds;

    std::vector<DbTableStats> largestTables;
    std::uint64_t checkedAt = 0;
};

void to_json(nlohmann::json& j, const DbTableStats& table);
void to_json(nlohmann::json& j, const DbStats& stats);

}
