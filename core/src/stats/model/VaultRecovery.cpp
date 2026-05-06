#include "stats/model/VaultRecovery.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableRecoveryValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

bool lastErrorHasNoNewerSuccess(const VaultRecovery& recovery) {
    if (!recovery.lastError) return false;
    if (!recovery.lastSuccessAt) return true;
    if (!recovery.lastBackupAt) return true;
    return *recovery.lastBackupAt > *recovery.lastSuccessAt;
}

}

void VaultRecovery::finalize() {
    if (!backupPolicyPresent) {
        backupStatus = "unknown";
        recoveryReadiness = "unknown";
        backupStale = std::nullopt;
        return;
    }

    if (!backupEnabled) {
        backupStatus = "disabled";
        recoveryReadiness = "disabled";
        backupStale = false;
        return;
    }

    if (backupStatus != "idle" && backupStatus != "syncing" && backupStatus != "error")
        backupStatus = "unknown";

    const auto now = checkedAt;
    if (lastSuccessAt) {
        lastSuccessAgeSeconds = now >= *lastSuccessAt ? now - *lastSuccessAt : 0;
        timeSinceLastVerifiedGoodState = lastSuccessAgeSeconds;
    }

    const auto baseline = lastSuccessAt ? lastSuccessAt : lastBackupAt;
    if (baseline && backupIntervalSeconds > 0)
        nextExpectedBackupAt = *baseline + backupIntervalSeconds;

    const auto staleThreshold = backupIntervalSeconds > 0 ? backupIntervalSeconds * 2 : std::uint64_t{0};
    const bool stale = !lastSuccessAt || (staleThreshold > 0 && lastSuccessAgeSeconds && *lastSuccessAgeSeconds > staleThreshold);
    backupStale = stale;

    if (backupIntervalSeconds > 0 && nextExpectedBackupAt && now > *nextExpectedBackupAt) {
        missedBackupCountEstimate = ((now - *nextExpectedBackupAt) / backupIntervalSeconds) + 1;
    } else {
        missedBackupCountEstimate = std::uint64_t{0};
    }

    if (backupStatus == "error" || lastErrorHasNoNewerSuccess(*this)) {
        recoveryReadiness = "failing";
    } else if (stale) {
        recoveryReadiness = "stale";
    } else if (lastSuccessAt && staleThreshold > 0 && lastSuccessAgeSeconds && *lastSuccessAgeSeconds <= staleThreshold) {
        recoveryReadiness = "ready";
    } else {
        recoveryReadiness = "unknown";
    }
}

void to_json(nlohmann::json& j, const VaultRecovery& recovery) {
    j = nlohmann::json{
        {"vault_id", recovery.vaultId},
        {"backup_policy_present", recovery.backupPolicyPresent},
        {"backup_enabled", recovery.backupEnabled},
        {"backup_status", recovery.backupStatus},
        {"backup_interval_seconds", recovery.backupIntervalSeconds},
        {"retention_seconds", nullableRecoveryValue(recovery.retentionSeconds)},
        {"last_backup_at", nullableRecoveryValue(recovery.lastBackupAt)},
        {"last_success_at", nullableRecoveryValue(recovery.lastSuccessAt)},
        {"last_success_age_seconds", nullableRecoveryValue(recovery.lastSuccessAgeSeconds)},
        {"next_expected_backup_at", nullableRecoveryValue(recovery.nextExpectedBackupAt)},
        {"backup_stale", nullableRecoveryValue(recovery.backupStale)},
        {"missed_backup_count_estimate", nullableRecoveryValue(recovery.missedBackupCountEstimate)},
        {"retry_count", recovery.retryCount},
        {"last_error", nullableRecoveryValue(recovery.lastError)},
        {"recovery_readiness", recovery.recoveryReadiness},
        {"time_since_last_verified_good_state", nullableRecoveryValue(recovery.timeSinceLastVerifiedGoodState)},
        {"checked_at", recovery.checkedAt},
    };
}

}
