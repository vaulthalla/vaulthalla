#include "db/query/vault/Activity.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/VaultActivity.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::vault {

namespace {

using vh::db::encoding::parsePostgresTimestamp;
using vh::stats::model::VaultActivity;
using vh::stats::model::VaultActivityEvent;
using vh::stats::model::VaultActivityTopPath;
using vh::stats::model::VaultActivityTopUser;

std::uint64_t unixTimestamp() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(now));
}

std::optional<std::uint64_t> optionalTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::uint64_t timestamp(const pqxx::row& row, const char* column) {
    const auto value = optionalTimestamp(row, column);
    return value.value_or(0);
}

std::optional<std::uint32_t> optionalUInt32(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<long long>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

std::uint64_t asUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<long long>();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::optional<std::string> optionalString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

void applySummary(VaultActivity& activity, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();

    activity.lastActivityAt = optionalTimestamp(row, "last_activity_at");
    activity.lastActivityAction = optionalString(row, "last_activity_action");
    activity.uploads24h = asUInt64(row, "uploads_24h");
    activity.uploads7d = asUInt64(row, "uploads_7d");
    activity.deletes24h = asUInt64(row, "deletes_24h");
    activity.deletes7d = asUInt64(row, "deletes_7d");
    activity.renames24h = asUInt64(row, "renames_24h");
    activity.renames7d = asUInt64(row, "renames_7d");
    activity.moves24h = asUInt64(row, "moves_24h");
    activity.moves7d = asUInt64(row, "moves_7d");
    activity.copies24h = asUInt64(row, "copies_24h");
    activity.copies7d = asUInt64(row, "copies_7d");
    activity.restores24h = asUInt64(row, "restores_24h");
    activity.restores7d = asUInt64(row, "restores_7d");
    activity.bytesAdded24h = asUInt64(row, "bytes_added_24h");
    activity.bytesRemoved24h = asUInt64(row, "bytes_removed_24h");
}

std::vector<VaultActivityTopUser> topUsersFromResult(const pqxx::result& res) {
    std::vector<VaultActivityTopUser> users;
    users.reserve(res.size());
    for (const auto& row : res) {
        users.push_back({
            .userId = optionalUInt32(row, "user_id"),
            .userName = optionalString(row, "user_name"),
            .count = asUInt64(row, "count"),
        });
    }
    return users;
}

std::vector<VaultActivityTopPath> topPathsFromResult(const pqxx::result& res) {
    std::vector<VaultActivityTopPath> paths;
    paths.reserve(res.size());
    for (const auto& row : res) {
        paths.push_back({
            .path = row["path"].as<std::string>(""),
            .action = row["action"].as<std::string>("unknown"),
            .count = asUInt64(row, "count"),
            .bytes = asUInt64(row, "bytes"),
        });
    }
    return paths;
}

std::vector<VaultActivityEvent> recentFromResult(const pqxx::result& res) {
    std::vector<VaultActivityEvent> events;
    events.reserve(res.size());
    for (const auto& row : res) {
        events.push_back({
            .source = row["source"].as<std::string>("unknown"),
            .action = row["action"].as<std::string>("unknown"),
            .path = row["path"].as<std::string>(""),
            .userId = optionalUInt32(row, "user_id"),
            .userName = optionalString(row, "user_name"),
            .status = optionalString(row, "status"),
            .error = optionalString(row, "error"),
            .bytes = asUInt64(row, "bytes"),
            .occurredAt = timestamp(row, "occurred_at"),
        });
    }
    return events;
}

}

std::shared_ptr<::vh::stats::model::VaultActivity> Activity::getVaultActivity(const std::uint32_t vaultId) {
    return Transactions::exec("VaultActivity::getVaultActivity", [&](pqxx::work& txn) {
        auto activity = std::make_shared<VaultActivity>();
        activity->vaultId = vaultId;
        activity->checkedAt = unixTimestamp();

        applySummary(*activity, txn.exec(pqxx::prepped{"vault_activity.summary"}, vaultId));
        activity->topActiveUsers = topUsersFromResult(txn.exec(pqxx::prepped{"vault_activity.top_users"}, vaultId));
        activity->topTouchedPaths = topPathsFromResult(txn.exec(pqxx::prepped{"vault_activity.top_paths"}, vaultId));
        activity->recentActivity = recentFromResult(txn.exec(pqxx::prepped{"vault_activity.recent"}, vaultId));

        return activity;
    });
}

}
