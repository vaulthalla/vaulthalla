#include "db/query/stats/Snapshot.hpp"

#include "db/Transactions.hpp"
#include "stats/model/StatsTrends.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace vh::db::query::stats {

namespace {

using ::vh::stats::model::StatsTrendPoint;
using ::vh::stats::model::StatsTrendSeries;
using ::vh::stats::model::StatsTrends;

std::uint64_t statsSnapshotUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::uint32_t clampWindowHours(std::uint32_t windowHours) {
    return std::clamp<std::uint32_t>(windowHours == 0 ? 168 : windowHours, 1, 24 * 30);
}

std::uint64_t statsSnapshotUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<long long>();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

double statsSnapshotDouble(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    return field.is_null() ? 0.0 : field.as<double>();
}

std::shared_ptr<StatsTrends> trendsFromResult(
    const std::string& scope,
    const std::optional<std::uint32_t>& vaultId,
    const std::uint32_t windowHours,
    const pqxx::result& rows
) {
    auto trends = std::make_shared<StatsTrends>();
    trends->scope = scope;
    trends->vaultId = vaultId;
    trends->windowHours = windowHours;
    trends->checkedAt = statsSnapshotUnixTimestamp();

    std::map<std::string, StatsTrendSeries> byKey;
    for (const auto& row : rows) {
        const auto key = row["metric_key"].as<std::string>("");
        if (key.empty()) continue;

        auto& series = byKey[key];
        if (series.key.empty()) {
            series.key = key;
            series.label = row["label"].as<std::string>("");
            series.unit = row["unit"].as<std::string>("");
            series.snapshotType = row["snapshot_type"].as<std::string>("");
        }

        series.points.push_back({
            .createdAt = statsSnapshotUInt64(row, "created_at"),
            .value = statsSnapshotDouble(row, "value"),
        });
    }

    trends->series.reserve(byKey.size());
    for (auto& [_, series] : byKey) {
        trends->series.push_back(std::move(series));
    }

    return trends;
}

}

void Snapshot::insertSystem(const std::string& snapshotType, const nlohmann::json& payload) {
    Transactions::exec("StatsSnapshot::insertSystem", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"stats_snapshot.insert_system"}, pqxx::params{snapshotType, payload.dump()});
    });
}

void Snapshot::insertVault(
    const std::uint32_t vaultId,
    const std::string& snapshotType,
    const nlohmann::json& payload
) {
    Transactions::exec("StatsSnapshot::insertVault", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"stats_snapshot.insert_vault"}, pqxx::params{vaultId, snapshotType, payload.dump()});
    });
}

void Snapshot::purgeOlderThan(const std::uint32_t retentionDays) {
    Transactions::exec("StatsSnapshot::purgeOlderThan", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"stats_snapshot.purge_older_than"}, pqxx::params{std::max<std::uint32_t>(1, retentionDays)});
    });
}

std::shared_ptr<::vh::stats::model::StatsTrends> Snapshot::systemTrends(std::uint32_t windowHours) {
    windowHours = clampWindowHours(windowHours);
    return Transactions::exec("StatsSnapshot::systemTrends", [&](pqxx::work& txn) {
        const auto rows = txn.exec(pqxx::prepped{"stats_snapshot.system_trends_typed"}, pqxx::params{windowHours});
        return trendsFromResult("system", std::nullopt, windowHours, rows);
    });
}

std::shared_ptr<::vh::stats::model::StatsTrends> Snapshot::vaultTrends(
    const std::uint32_t vaultId,
    std::uint32_t windowHours
) {
    windowHours = clampWindowHours(windowHours);
    return Transactions::exec("StatsSnapshot::vaultTrends", [&](pqxx::work& txn) {
        const auto rows = txn.exec(pqxx::prepped{"stats_snapshot.vault_trends_typed"}, pqxx::params{vaultId, windowHours});
        return trendsFromResult("vault", vaultId, windowHours, rows);
    });
}

}
