#include "stats/model/OperationStats.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableOperationValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void OperationStats::finalize() {
    const auto stalled = stalledOperations + stalledShareUploads;
    const auto failed = failedOperations24h + failedShareUploads24h;

    if (stalled > 0) {
        overallStatus = "critical";
    } else if (failed > 0 || cancelledOperations24h > 0 || pendingOperations > 0 || inProgressOperations > 0 || activeShareUploads > 0) {
        overallStatus = "warning";
    } else {
        overallStatus = "healthy";
    }
}

void to_json(nlohmann::json& j, const RecentOperationError& error) {
    j = nlohmann::json{
        {"source", error.source},
        {"operation", error.operation},
        {"status", error.status},
        {"target", error.target},
        {"path", error.path},
        {"error", nullableOperationValue(error.error)},
        {"occurred_at", nullableOperationValue(error.occurredAt)},
    };
}

void to_json(nlohmann::json& j, const OperationStats& stats) {
    j = nlohmann::json{
        {"vault_id", nullableOperationValue(stats.vaultId)},
        {"scope", stats.scope},
        {"overall_status", stats.overallStatus},
        {"stale_threshold_seconds", stats.staleThresholdSeconds},
        {"pending_operations", stats.pendingOperations},
        {"in_progress_operations", stats.inProgressOperations},
        {"failed_operations_24h", stats.failedOperations24h},
        {"cancelled_operations_24h", stats.cancelledOperations24h},
        {"stalled_operations", stats.stalledOperations},
        {"oldest_pending_operation_age_seconds", nullableOperationValue(stats.oldestPendingOperationAgeSeconds)},
        {"oldest_in_progress_operation_age_seconds", nullableOperationValue(stats.oldestInProgressOperationAgeSeconds)},
        {"operations_by_type", stats.operationsByType},
        {"operations_by_status", stats.operationsByStatus},
        {"active_share_uploads", stats.activeShareUploads},
        {"stalled_share_uploads", stats.stalledShareUploads},
        {"failed_share_uploads_24h", stats.failedShareUploads24h},
        {"upload_bytes_expected_active", stats.uploadBytesExpectedActive},
        {"upload_bytes_received_active", stats.uploadBytesReceivedActive},
        {"oldest_active_upload_age_seconds", nullableOperationValue(stats.oldestActiveUploadAgeSeconds)},
        {"recent_operation_errors", stats.recentOperationErrors},
        {"checked_at", stats.checkedAt},
    };
}

}
