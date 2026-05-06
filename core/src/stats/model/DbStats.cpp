#include "stats/model/DbStats.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableDbValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const DbTableStats& table) {
    j = nlohmann::json{
        {"table_name", table.tableName},
        {"total_bytes", table.totalBytes},
        {"table_bytes", table.tableBytes},
        {"index_bytes", table.indexBytes},
        {"row_estimate", table.rowEstimate},
    };
}

void to_json(nlohmann::json& j, const DbStats& stats) {
    j = nlohmann::json{
        {"connected", stats.connected},
        {"status", stats.status},
        {"error", nullableDbValue(stats.error)},
        {"database_name", nullableDbValue(stats.databaseName)},
        {"db_size_bytes", stats.dbSizeBytes},
        {"connections_active", stats.connectionsActive},
        {"connections_idle", stats.connectionsIdle},
        {"connections_idle_in_transaction", stats.connectionsIdleInTransaction},
        {"connections_total", stats.connectionsTotal},
        {"connections_max", nullableDbValue(stats.connectionsMax)},
        {"cache_hit_ratio", nullableDbValue(stats.cacheHitRatio)},
        {"pg_stat_statements_enabled", stats.pgStatStatementsEnabled},
        {"slow_query_count", nullableDbValue(stats.slowQueryCount)},
        {"deadlocks", stats.deadlocks},
        {"temp_bytes", stats.tempBytes},
        {"oldest_transaction_age_seconds", nullableDbValue(stats.oldestTransactionAgeSeconds)},
        {"largest_tables", stats.largestTables},
        {"checked_at", stats.checkedAt},
    };
}

}
