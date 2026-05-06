#include "db/query/stats/DbStats.hpp"

#include "db/Transactions.hpp"
#include "stats/model/DbStats.hpp"

#include <chrono>
#include <exception>
#include <pqxx/pqxx>

namespace vh::db::query::stats {

namespace {

using vh::stats::model::DbTableStats;

std::uint64_t unixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::uint64_t asUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<long long>();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::optional<std::uint64_t> optionalUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<long long>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

std::optional<double> optionalDouble(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return field.as<double>();
}

std::optional<std::string> optionalString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

void applySummary(vh::stats::model::DbStats& stats, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();
    stats.databaseName = optionalString(row, "database_name");
    stats.dbSizeBytes = asUInt64(row, "db_size_bytes");
    stats.connectionsMax = optionalUInt64(row, "connections_max");
    stats.cacheHitRatio = optionalDouble(row, "cache_hit_ratio");
    stats.deadlocks = asUInt64(row, "deadlocks");
    stats.tempBytes = asUInt64(row, "temp_bytes");
    stats.oldestTransactionAgeSeconds = optionalUInt64(row, "oldest_transaction_age_seconds");
}

void applyConnections(vh::stats::model::DbStats& stats, const pqxx::result& res) {
    for (const auto& row : res) {
        const auto state = row["state"].as<std::string>("");
        const auto count = asUInt64(row, "count");
        stats.connectionsTotal += count;
        if (state == "active") stats.connectionsActive += count;
        else if (state == "idle") stats.connectionsIdle += count;
        else if (state == "idle in transaction") stats.connectionsIdleInTransaction += count;
    }
}

std::vector<DbTableStats> tablesFromResult(const pqxx::result& res) {
    std::vector<DbTableStats> tables;
    tables.reserve(res.size());
    for (const auto& row : res) {
        tables.push_back({
            .tableName = row["table_name"].as<std::string>(""),
            .totalBytes = asUInt64(row, "total_bytes"),
            .tableBytes = asUInt64(row, "table_bytes"),
            .indexBytes = asUInt64(row, "index_bytes"),
            .rowEstimate = asUInt64(row, "row_estimate"),
        });
    }
    return tables;
}

std::string classify(const vh::stats::model::DbStats& stats) {
    if (!stats.connected) return "critical";

    if (stats.connectionsMax && *stats.connectionsMax > 0) {
        const auto ratio = static_cast<double>(stats.connectionsTotal) / static_cast<double>(*stats.connectionsMax);
        if (ratio >= 0.95) return "critical";
        if (ratio >= 0.80) return "warning";
    }

    if (stats.oldestTransactionAgeSeconds && *stats.oldestTransactionAgeSeconds >= 86400) return "critical";
    if (stats.deadlocks > 0) return "warning";
    if (stats.oldestTransactionAgeSeconds && *stats.oldestTransactionAgeSeconds >= 3600) return "warning";
    if (stats.cacheHitRatio && *stats.cacheHitRatio > 0.0 && *stats.cacheHitRatio < 0.80) return "warning";

    return "healthy";
}

std::optional<std::uint64_t> slowQueryCount() {
    try {
        return Transactions::exec("DbStats::slowQueryCount", [](pqxx::work& txn) -> std::optional<std::uint64_t> {
            const auto res = txn.exec(R"SQL(
                SELECT COUNT(*) AS count
                FROM pg_stat_statements
                WHERE calls > 0
                  AND mean_exec_time >= 1000.0;
            )SQL");

            if (res.empty()) return 0;
            return asUInt64(res.one_row(), "count");
        });
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}

std::shared_ptr<vh::stats::model::DbStats> DbStats::snapshot() {
    auto stats = std::make_shared<vh::stats::model::DbStats>();
    stats->checkedAt = unixTimestamp();

    try {
        Transactions::exec("DbStats::snapshot", [&](pqxx::work& txn) {
            stats->connected = true;
            applySummary(*stats, txn.exec(pqxx::prepped{"db_stats.summary"}));
            applyConnections(*stats, txn.exec(pqxx::prepped{"db_stats.connections"}));
            const auto extension = txn.exec(pqxx::prepped{"db_stats.pg_stat_statements_enabled"});
            stats->pgStatStatementsEnabled = !extension.empty() && extension.one_row()["enabled"].as<bool>(false);
            stats->largestTables = tablesFromResult(txn.exec(pqxx::prepped{"db_stats.largest_tables"}));
        });

        if (stats->pgStatStatementsEnabled) stats->slowQueryCount = slowQueryCount();
    } catch (const std::exception& e) {
        stats->connected = false;
        stats->status = "critical";
        stats->error = e.what();
        return stats;
    }

    stats->status = classify(*stats);
    return stats;
}

}
