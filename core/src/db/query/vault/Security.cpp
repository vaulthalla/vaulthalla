#include "db/query/vault/Security.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/VaultSecurity.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::vault {

namespace {

using vh::db::encoding::parsePostgresTimestamp;
using vh::stats::model::VaultSecurity;

std::uint64_t securityUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::optional<std::uint64_t> optionalSecurityTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::optional<std::uint32_t> optionalSecurityUInt32(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<long long>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

std::optional<std::uint64_t> optionalSecurityUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<long long>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

std::uint64_t asSecurityUInt64(const pqxx::row& row, const char* column) {
    return optionalSecurityUInt64(row, column).value_or(0);
}

std::optional<std::string> optionalSecurityString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

std::string encryptionStatus(const VaultSecurity& security) {
    if (!security.currentKeyVersion) return "unknown";
    if (security.filesLegacyKeyVersion > 0 || security.filesUnknownKeyVersion > 0) return "mixed";
    return "encrypted";
}

std::string overallStatus(const VaultSecurity& security) {
    if (security.encryptionStatus == "unknown") return "unknown";
    if (security.filesLegacyKeyVersion > 0 || security.filesUnknownKeyVersion > 0) return "warning";
    if (security.unauthorizedAccessAttempts24h > 0 || security.rateLimitedAttempts24h > 0) return "warning";
    return "healthy";
}

void applySummary(VaultSecurity& security, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();

    security.currentKeyVersion = optionalSecurityUInt32(row, "current_key_version");
    security.keyCreatedAt = optionalSecurityTimestamp(row, "key_created_at");
    security.keyAgeDays = optionalSecurityUInt64(row, "key_age_days");
    security.trashedKeyVersionsCount = asSecurityUInt64(row, "trashed_key_versions_count");
    security.fileCount = asSecurityUInt64(row, "file_count");
    security.filesCurrentKeyVersion = asSecurityUInt64(row, "files_current_key_version");
    security.filesLegacyKeyVersion = asSecurityUInt64(row, "files_legacy_key_version");
    security.filesUnknownKeyVersion = asSecurityUInt64(row, "files_unknown_key_version");
    security.unauthorizedAccessAttempts24h = asSecurityUInt64(row, "unauthorized_access_attempts_24h");
    security.rateLimitedAttempts24h = asSecurityUInt64(row, "rate_limited_attempts_24h");
    security.lastDeniedAccessAt = optionalSecurityTimestamp(row, "last_denied_access_at");
    security.lastDeniedAccessReason = optionalSecurityString(row, "last_denied_access_reason");
    security.lastPermissionChangeAt = optionalSecurityTimestamp(row, "last_permission_change_at");
    security.lastSharePolicyChangeAt = optionalSecurityTimestamp(row, "last_share_policy_change_at");
}

}

std::shared_ptr<::vh::stats::model::VaultSecurity> Security::getVaultSecurity(const std::uint32_t vaultId) {
    return Transactions::exec("VaultSecurity::getVaultSecurity", [&](pqxx::work& txn) {
        auto security = std::make_shared<VaultSecurity>();
        security->vaultId = vaultId;
        security->checkedAt = securityUnixTimestamp();

        applySummary(*security, txn.exec(pqxx::prepped{"vault_security.summary"}, vaultId));
        security->encryptionStatus = encryptionStatus(*security);
        security->overallStatus = overallStatus(*security);

        return security;
    });
}

}
