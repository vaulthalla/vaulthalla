#include "db/query/stats/OperationStats.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/OperationStats.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::stats {

namespace {

using Model = ::vh::stats::model::OperationStats;
using RecentError = ::vh::stats::model::RecentOperationError;
using vh::db::encoding::parsePostgresTimestamp;

std::uint64_t operationStatsUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::optional<std::uint64_t> optionalOperationStatsTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::optional<std::uint64_t> optionalOperationStatsUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<double>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

std::uint64_t operationStatsUInt64(const pqxx::row& row, const char* column) {
    return optionalOperationStatsUInt64(row, column).value_or(0);
}

std::string operationStatsString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    return field.is_null() ? std::string{} : field.as<std::string>();
}

std::optional<std::string> optionalOperationStatsString(const pqxx::row& row, const char* column) {
    const auto value = operationStatsString(row, column);
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

void applyOperations(Model& stats, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();

    stats.pendingOperations = operationStatsUInt64(row, "pending_operations");
    stats.inProgressOperations = operationStatsUInt64(row, "in_progress_operations");
    stats.failedOperations24h = operationStatsUInt64(row, "failed_operations_24h");
    stats.cancelledOperations24h = operationStatsUInt64(row, "cancelled_operations_24h");
    stats.stalledOperations = operationStatsUInt64(row, "stalled_operations");
    stats.oldestPendingOperationAgeSeconds = optionalOperationStatsUInt64(row, "oldest_pending_operation_age_seconds");
    stats.oldestInProgressOperationAgeSeconds = optionalOperationStatsUInt64(row, "oldest_in_progress_operation_age_seconds");

    stats.operationsByType["move"] = operationStatsUInt64(row, "move_count");
    stats.operationsByType["copy"] = operationStatsUInt64(row, "copy_count");
    stats.operationsByType["rename"] = operationStatsUInt64(row, "rename_count");
    stats.operationsByStatus["pending"] = operationStatsUInt64(row, "status_pending_count");
    stats.operationsByStatus["in_progress"] = operationStatsUInt64(row, "status_in_progress_count");
    stats.operationsByStatus["success"] = operationStatsUInt64(row, "status_success_count");
    stats.operationsByStatus["error"] = operationStatsUInt64(row, "status_error_count");
    stats.operationsByStatus["cancelled"] = operationStatsUInt64(row, "status_cancelled_count");
}

void applyUploads(Model& stats, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();

    stats.activeShareUploads = operationStatsUInt64(row, "active_share_uploads");
    stats.stalledShareUploads = operationStatsUInt64(row, "stalled_share_uploads");
    stats.failedShareUploads24h = operationStatsUInt64(row, "failed_share_uploads_24h");
    stats.uploadBytesExpectedActive = operationStatsUInt64(row, "upload_bytes_expected_active");
    stats.uploadBytesReceivedActive = operationStatsUInt64(row, "upload_bytes_received_active");
    stats.oldestActiveUploadAgeSeconds = optionalOperationStatsUInt64(row, "oldest_active_upload_age_seconds");
}

void applyRecentErrors(Model& stats, const pqxx::result& res) {
    stats.recentOperationErrors.clear();
    stats.recentOperationErrors.reserve(res.size());

    for (const auto& row : res) {
        stats.recentOperationErrors.push_back(RecentError{
            .source = operationStatsString(row, "source"),
            .operation = operationStatsString(row, "operation"),
            .status = operationStatsString(row, "status"),
            .target = operationStatsString(row, "target"),
            .path = operationStatsString(row, "path"),
            .error = optionalOperationStatsString(row, "error"),
            .occurredAt = optionalOperationStatsTimestamp(row, "occurred_at"),
        });
    }
}

std::shared_ptr<Model> snapshotWithQueries(
    pqxx::work& txn,
    const std::optional<std::uint32_t> vaultId,
    const char* operationsQuery,
    const char* uploadsQuery,
    const char* errorsQuery
) {
    auto stats = std::make_shared<Model>();
    stats->vaultId = vaultId;
    stats->scope = vaultId ? "vault" : "system";
    stats->checkedAt = operationStatsUnixTimestamp();

    if (vaultId) {
        applyOperations(*stats, txn.exec(pqxx::prepped{operationsQuery}, *vaultId));
        applyUploads(*stats, txn.exec(pqxx::prepped{uploadsQuery}, *vaultId));
        applyRecentErrors(*stats, txn.exec(pqxx::prepped{errorsQuery}, *vaultId));
    } else {
        applyOperations(*stats, txn.exec(pqxx::prepped{operationsQuery}));
        applyUploads(*stats, txn.exec(pqxx::prepped{uploadsQuery}));
        applyRecentErrors(*stats, txn.exec(pqxx::prepped{errorsQuery}));
    }

    stats->finalize();
    return stats;
}

}

std::shared_ptr<::vh::stats::model::OperationStats> OperationStats::snapshot() {
    return Transactions::exec("OperationStats::snapshot", [&](pqxx::work& txn) {
        return snapshotWithQueries(
            txn,
            std::nullopt,
            "operation_stats.system_operations",
            "operation_stats.system_uploads",
            "operation_stats.system_recent_errors"
        );
    });
}

std::shared_ptr<::vh::stats::model::OperationStats> OperationStats::snapshotForVault(const std::uint32_t vaultId) {
    return Transactions::exec("OperationStats::snapshotForVault", [&](pqxx::work& txn) {
        return snapshotWithQueries(
            txn,
            vaultId,
            "operation_stats.vault_operations",
            "operation_stats.vault_uploads",
            "operation_stats.vault_recent_errors"
        );
    });
}

}
