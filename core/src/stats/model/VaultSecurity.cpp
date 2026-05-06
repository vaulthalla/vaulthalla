#include "stats/model/VaultSecurity.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableSecurityValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const VaultSecurity& security) {
    j = nlohmann::json{
        {"vault_id", security.vaultId},
        {"overall_status", security.overallStatus},
        {"encryption_status", security.encryptionStatus},
        {"current_key_version", nullableSecurityValue(security.currentKeyVersion)},
        {"key_created_at", nullableSecurityValue(security.keyCreatedAt)},
        {"key_age_days", nullableSecurityValue(security.keyAgeDays)},
        {"trashed_key_versions_count", security.trashedKeyVersionsCount},
        {"file_count", security.fileCount},
        {"files_current_key_version", security.filesCurrentKeyVersion},
        {"files_legacy_key_version", security.filesLegacyKeyVersion},
        {"files_unknown_key_version", security.filesUnknownKeyVersion},
        {"unauthorized_access_attempts_24h", security.unauthorizedAccessAttempts24h},
        {"rate_limited_attempts_24h", security.rateLimitedAttempts24h},
        {"last_denied_access_at", nullableSecurityValue(security.lastDeniedAccessAt)},
        {"last_denied_access_reason", nullableSecurityValue(security.lastDeniedAccessReason)},
        {"last_permission_change_at", nullableSecurityValue(security.lastPermissionChangeAt)},
        {"last_share_policy_change_at", nullableSecurityValue(security.lastSharePolicyChangeAt)},
        {"integrity_check_status", security.integrityCheckStatus},
        {"last_integrity_check_at", nullableSecurityValue(security.lastIntegrityCheckAt)},
        {"checksum_mismatch_count", nullableSecurityValue(security.checksumMismatchCount)},
        {"checked_at", security.checkedAt},
    };
}

}
