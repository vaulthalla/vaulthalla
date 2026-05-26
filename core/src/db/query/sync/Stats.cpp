#include "db/query/sync/Stats.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/VaultSyncHealth.hpp"
#include "sync/model/Event.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::sync {

namespace {

using Health = vh::stats::model::VaultSyncHealth;
using vh::db::encoding::parsePostgresTimestamp;

std::uint64_t unixTimestamp() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(now));
}

std::optional<std::uint64_t> optionalTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::optional<std::string> optionalString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

std::optional<std::uint64_t> optionalUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<double>();
    if (value < 0) return std::uint64_t{0};
    return static_cast<std::uint64_t>(value);
}

std::uint64_t asUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<double>();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

double asDouble(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    return field.is_null() ? 0.0 : field.as<double>();
}

void applyConfig(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.syncConfigPresent = true;
    health.syncEnabled = !row["enabled"].is_null() && row["enabled"].as<bool>();
    health.syncIntervalSeconds = row["interval"].is_null() ? 0 : row["interval"].as<std::uint64_t>();
    health.configuredStrategy = optionalString(row, "configured_strategy");
    health.conflictPolicy = optionalString(row, "conflict_policy");
    health.s3BudgetListRequests = optionalUInt64(row, "s3_budget_list_requests");
    health.s3BudgetHeadRequests = optionalUInt64(row, "s3_budget_head_requests");
    health.s3BudgetGetRequests = optionalUInt64(row, "s3_budget_get_requests");
    health.s3BudgetPutRequests = optionalUInt64(row, "s3_budget_put_requests");
    health.s3BudgetCopyRequests = optionalUInt64(row, "s3_budget_copy_requests");
    health.s3BudgetDeleteRequests = optionalUInt64(row, "s3_budget_delete_requests");
    health.s3BudgetDownloadedBytes = optionalUInt64(row, "s3_budget_downloaded_bytes");
    health.s3BudgetUnlimitedLegacy =
        health.configuredStrategy.has_value() &&
        !health.s3BudgetListRequests &&
        !health.s3BudgetHeadRequests &&
        !health.s3BudgetGetRequests &&
        !health.s3BudgetPutRequests &&
        !health.s3BudgetCopyRequests &&
        !health.s3BudgetDeleteRequests &&
        !health.s3BudgetDownloadedBytes;
    if (health.s3BudgetUnlimitedLegacy) {
        health.s3BudgetWarning =
            "Unlimited/legacy S3 budget: no S3 request or downloaded-byte guardrails are enforced. "
            "Set the balanced preset to enable guardrails.";
    }
    health.maxRemoteIndexAgeSeconds = optionalUInt64(row, "max_remote_index_age_seconds");

    health.lastSyncAt = optionalTimestamp(row, "last_sync_at");
    health.lastSuccessAt = optionalTimestamp(row, "last_success_at");

    if (health.lastSuccessAt) {
        const auto now = unixTimestamp();
        health.lastSuccessAgeSeconds = now >= *health.lastSuccessAt ? now - *health.lastSuccessAt : 0;
    }
}

void applyLatest(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.syncHistoryPresent = true;
    health.latestEvent = std::make_shared<vh::sync::model::Event>(row);
    health.latestEventId = health.latestEvent->id;
    health.latestRunUuid = health.latestEvent->run_uuid;
    health.divergenceDetected = health.latestEvent->divergence_detected;
    health.localStateHash = health.latestEvent->local_state_hash.empty()
        ? std::nullopt
        : std::optional<std::string>(health.latestEvent->local_state_hash);
    health.remoteStateHash = health.latestEvent->remote_state_hash.empty()
        ? std::nullopt
        : std::optional<std::string>(health.latestEvent->remote_state_hash);
    health.lastStallReason = health.latestEvent->stall_reason.empty()
        ? std::nullopt
        : std::optional<std::string>(health.latestEvent->stall_reason);
}

void applyActiveSummary(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.activeRunCount = asUInt64(row, "active_run_count");
    health.pendingRunCount = asUInt64(row, "pending_run_count");
    health.runningRunCount = asUInt64(row, "running_run_count");
    health.stalledRunCount = asUInt64(row, "stalled_run_count");
    health.oldestActiveAgeSeconds = optionalUInt64(row, "oldest_active_age_seconds");
    health.latestHeartbeatAgeSeconds = optionalUInt64(row, "latest_heartbeat_age_seconds");
}

void applyEventWindows(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.errorCount24h = asUInt64(row, "error_count_24h");
    health.errorCount7d = asUInt64(row, "error_count_7d");
    health.failedOps24h = asUInt64(row, "failed_ops_24h");
    health.failedOps7d = asUInt64(row, "failed_ops_7d");
    health.retryCount24h = asUInt64(row, "retry_count_24h");
    health.retryCount7d = asUInt64(row, "retry_count_7d");
    health.bytesUp24h = asUInt64(row, "bytes_up_24h");
    health.bytesDown24h = asUInt64(row, "bytes_down_24h");
    health.bytesUp7d = asUInt64(row, "bytes_up_7d");
    health.bytesDown7d = asUInt64(row, "bytes_down_7d");
}

void applyConflictWindows(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.conflictCountOpen = asUInt64(row, "conflict_count_open");
    health.conflictCount24h = asUInt64(row, "conflict_count_24h");
    health.conflictCount7d = asUInt64(row, "conflict_count_7d");
}

void applyThroughput(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.avgThroughputBytesPerSec24h = asDouble(row, "avg_throughput_bytes_per_sec_24h");
    health.peakThroughputBytesPerSec24h = asDouble(row, "peak_throughput_bytes_per_sec_24h");
}

void applyRemoteIndex(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.remoteIndexObjectCount = asUInt64(row, "object_count");
    health.remoteIndexSource = optionalString(row, "source");
    if (!row["indexed_at"].is_null()) {
        health.remoteIndexIndexedAt = static_cast<std::uint64_t>(
            parsePostgresTimestamp(row["indexed_at"].as<std::string>()));
    }
}

void applyLastError(Health& health, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    health.lastErrorCode = optionalString(row, "error_code");
    health.lastErrorMessage = optionalString(row, "error_message");
    health.lastStallReason = health.lastStallReason ? health.lastStallReason : optionalString(row, "stall_reason");

    const auto lastErrorAt = optionalTimestamp(row, "timestamp_begin");
    health.lastErrorAfterLastSuccess = lastErrorAt && (!health.lastSuccessAt || *lastErrorAt > *health.lastSuccessAt);
}

}

std::shared_ptr<vh::stats::model::VaultSyncHealth> Stats::getVaultSyncHealth(const unsigned int vaultId) {
    return Transactions::exec("SyncStats::getVaultSyncHealth", [&](pqxx::work& txn) {
        auto health = std::make_shared<Health>();
        health->vaultId = vaultId;
        health->checkedAt = unixTimestamp();

        applyConfig(*health, txn.exec(pqxx::prepped{"sync_stats.config"}, vaultId));
        applyLatest(*health, txn.exec(pqxx::prepped{"sync_stats.latest_event"}, vaultId));
        applyActiveSummary(*health, txn.exec(pqxx::prepped{"sync_stats.active_summary"}, vaultId));
        applyEventWindows(*health, txn.exec(pqxx::prepped{"sync_stats.event_windows"}, vaultId));
        applyConflictWindows(*health, txn.exec(pqxx::prepped{"sync_stats.conflict_windows"}, vaultId));
        applyThroughput(*health, txn.exec(pqxx::prepped{"sync_stats.throughput_24h"}, vaultId));
        applyRemoteIndex(*health, txn.exec(pqxx::prepped{"remote_object_index.summary_for_vault"}, vaultId));
        applyLastError(*health, txn.exec(pqxx::prepped{"sync_stats.last_error"}, vaultId));

        health->finalize();
        return health;
    });
}

}
