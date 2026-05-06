#include "stats/model/VaultSyncHealth.hpp"

#include "sync/model/Event.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

constexpr std::uint64_t kStaleHeartbeatWarningSeconds = 60;
constexpr std::uint64_t kStalledHeartbeatSeconds = 120;

std::string latestStatus(const std::shared_ptr<sync::model::Event>& event) {
    return event ? std::string(sync::model::Event::toString(event->status)) : "";
}

}

void VaultSyncHealth::finalize() {
    bytesTotal24h = bytesUp24h + bytesDown24h;
    bytesTotal7d = bytesUp7d + bytesDown7d;

    hashMismatch = localStateHash && remoteStateHash && *localStateHash != *remoteStateHash;
    divergenceDetected = divergenceDetected || hashMismatch;

    if (!syncConfigPresent && !syncHistoryPresent) {
        overallStatus = VaultSyncOverallStatus::Unknown;
        currentState = VaultSyncCurrentState::Unknown;
        return;
    }

    const auto status = latestStatus(latestEvent);

    if (divergenceDetected) currentState = VaultSyncCurrentState::Diverged;
    else if (stalledRunCount > 0 || status == "stalled" ||
        (runningRunCount > 0 && latestHeartbeatAgeSeconds && *latestHeartbeatAgeSeconds >= kStalledHeartbeatSeconds)) {
        currentState = VaultSyncCurrentState::Stalled;
    } else if (runningRunCount > 0 || status == "running") {
        currentState = VaultSyncCurrentState::Running;
    } else if (pendingRunCount > 0 || status == "pending") {
        currentState = VaultSyncCurrentState::Pending;
    } else if (status == "error") {
        currentState = VaultSyncCurrentState::Error;
    } else if (status == "cancelled") {
        currentState = VaultSyncCurrentState::Cancelled;
    } else if (status == "success") {
        currentState = activeRunCount == 0 ? VaultSyncCurrentState::Idle : VaultSyncCurrentState::Success;
    } else if (syncConfigPresent && !syncHistoryPresent) {
        currentState = VaultSyncCurrentState::Unknown;
    } else {
        currentState = VaultSyncCurrentState::Unknown;
    }

    if (!syncHistoryPresent) {
        overallStatus = VaultSyncOverallStatus::Unknown;
    } else if (divergenceDetected) {
        overallStatus = VaultSyncOverallStatus::Diverged;
    } else if (currentState == VaultSyncCurrentState::Stalled) {
        overallStatus = VaultSyncOverallStatus::Stalled;
    } else if (status == "error" || lastErrorAfterLastSuccess || failedOps24h > 0) {
        overallStatus = VaultSyncOverallStatus::Error;
    } else if (pendingRunCount > 0 || runningRunCount > 0) {
        overallStatus = VaultSyncOverallStatus::Syncing;
    } else if (
        errorCount24h > 0 ||
        errorCount7d > 0 ||
        conflictCountOpen > 0 ||
        retryCount24h > 0 ||
        (latestHeartbeatAgeSeconds && *latestHeartbeatAgeSeconds >= kStaleHeartbeatWarningSeconds)
    ) {
        overallStatus = VaultSyncOverallStatus::Warning;
    } else {
        overallStatus = VaultSyncOverallStatus::Healthy;
    }
}

std::string to_string(const VaultSyncOverallStatus status) {
    switch (status) {
        case VaultSyncOverallStatus::Healthy:
            return "healthy";
        case VaultSyncOverallStatus::Syncing:
            return "syncing";
        case VaultSyncOverallStatus::Warning:
            return "warning";
        case VaultSyncOverallStatus::Stalled:
            return "stalled";
        case VaultSyncOverallStatus::Error:
            return "error";
        case VaultSyncOverallStatus::Diverged:
            return "diverged";
        case VaultSyncOverallStatus::Unknown:
        default:
            return "unknown";
    }
}

std::string to_string(const VaultSyncCurrentState state) {
    switch (state) {
        case VaultSyncCurrentState::Idle:
            return "idle";
        case VaultSyncCurrentState::Pending:
            return "pending";
        case VaultSyncCurrentState::Running:
            return "running";
        case VaultSyncCurrentState::Stalled:
            return "stalled";
        case VaultSyncCurrentState::Error:
            return "error";
        case VaultSyncCurrentState::Cancelled:
            return "cancelled";
        case VaultSyncCurrentState::Success:
            return "success";
        case VaultSyncCurrentState::Diverged:
            return "diverged";
        case VaultSyncCurrentState::Unknown:
        default:
            return "unknown";
    }
}

void to_json(nlohmann::json& j, const VaultSyncHealth& health) {
    j = nlohmann::json{
        {"vault_id", health.vaultId},
        {"overall_status", to_string(health.overallStatus)},
        {"current_state", to_string(health.currentState)},
        {"sync_enabled", health.syncEnabled},
        {"sync_interval_seconds", health.syncIntervalSeconds},
        {"configured_strategy", health.configuredStrategy ? nlohmann::json(*health.configuredStrategy) : nlohmann::json(nullptr)},
        {"conflict_policy", health.conflictPolicy ? nlohmann::json(*health.conflictPolicy) : nlohmann::json(nullptr)},
        {"latest_event", health.latestEvent},
        {"latest_event_id", health.latestEventId ? nlohmann::json(*health.latestEventId) : nlohmann::json(nullptr)},
        {"latest_run_uuid", health.latestRunUuid ? nlohmann::json(*health.latestRunUuid) : nlohmann::json(nullptr)},
        {"last_sync_at", health.lastSyncAt ? nlohmann::json(*health.lastSyncAt) : nlohmann::json(nullptr)},
        {"last_success_at", health.lastSuccessAt ? nlohmann::json(*health.lastSuccessAt) : nlohmann::json(nullptr)},
        {"last_success_age_seconds", health.lastSuccessAgeSeconds ? nlohmann::json(*health.lastSuccessAgeSeconds) : nlohmann::json(nullptr)},
        {"active_run_count", health.activeRunCount},
        {"pending_run_count", health.pendingRunCount},
        {"running_run_count", health.runningRunCount},
        {"stalled_run_count", health.stalledRunCount},
        {"oldest_active_age_seconds", health.oldestActiveAgeSeconds ? nlohmann::json(*health.oldestActiveAgeSeconds) : nlohmann::json(nullptr)},
        {"latest_heartbeat_age_seconds", health.latestHeartbeatAgeSeconds ? nlohmann::json(*health.latestHeartbeatAgeSeconds) : nlohmann::json(nullptr)},
        {"error_count_24h", health.errorCount24h},
        {"error_count_7d", health.errorCount7d},
        {"failed_ops_24h", health.failedOps24h},
        {"failed_ops_7d", health.failedOps7d},
        {"retry_count_24h", health.retryCount24h},
        {"retry_count_7d", health.retryCount7d},
        {"conflict_count_open", health.conflictCountOpen},
        {"conflict_count_24h", health.conflictCount24h},
        {"conflict_count_7d", health.conflictCount7d},
        {"bytes_up_24h", health.bytesUp24h},
        {"bytes_down_24h", health.bytesDown24h},
        {"bytes_total_24h", health.bytesTotal24h},
        {"bytes_up_7d", health.bytesUp7d},
        {"bytes_down_7d", health.bytesDown7d},
        {"bytes_total_7d", health.bytesTotal7d},
        {"avg_throughput_bytes_per_sec_24h", health.avgThroughputBytesPerSec24h},
        {"peak_throughput_bytes_per_sec_24h", health.peakThroughputBytesPerSec24h},
        {"divergence_detected", health.divergenceDetected},
        {"local_state_hash", health.localStateHash ? nlohmann::json(*health.localStateHash) : nlohmann::json(nullptr)},
        {"remote_state_hash", health.remoteStateHash ? nlohmann::json(*health.remoteStateHash) : nlohmann::json(nullptr)},
        {"hash_mismatch", health.hashMismatch},
        {"last_error_code", health.lastErrorCode ? nlohmann::json(*health.lastErrorCode) : nlohmann::json(nullptr)},
        {"last_error_message", health.lastErrorMessage ? nlohmann::json(*health.lastErrorMessage) : nlohmann::json(nullptr)},
        {"last_stall_reason", health.lastStallReason ? nlohmann::json(*health.lastStallReason) : nlohmann::json(nullptr)},
        {"checked_at", health.checkedAt},
    };
}

void to_json(nlohmann::json& j, const std::shared_ptr<VaultSyncHealth>& health) {
    j = health ? nlohmann::json(*health) : nlohmann::json(nullptr);
}

}
