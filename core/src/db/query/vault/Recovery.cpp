#include "db/query/vault/Recovery.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/VaultRecovery.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::vault {

namespace {

using vh::db::encoding::parsePostgresTimestamp;
using vh::stats::model::VaultRecovery;

std::uint64_t recoveryUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::optional<std::uint64_t> optionalRecoveryTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::optional<std::uint64_t> optionalRecoveryUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<double>();
    if (value < 0) return std::nullopt;
    return static_cast<std::uint64_t>(value);
}

std::uint64_t recoveryUInt64(const pqxx::row& row, const char* column) {
    return optionalRecoveryUInt64(row, column).value_or(0);
}

std::optional<std::string> optionalRecoveryString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

void applyPolicy(VaultRecovery& recovery, const pqxx::result& res) {
    if (res.empty()) return;

    const auto row = res.one_row();
    recovery.backupPolicyPresent = true;
    recovery.backupEnabled = !row["enabled"].is_null() && row["enabled"].as<bool>();
    recovery.backupStatus = recovery.backupEnabled ? row["status"].as<std::string>("unknown") : "disabled";
    recovery.backupIntervalSeconds = recoveryUInt64(row, "backup_interval");
    recovery.retentionSeconds = optionalRecoveryUInt64(row, "retention_seconds");
    recovery.lastBackupAt = optionalRecoveryTimestamp(row, "last_backup_at");
    recovery.lastSuccessAt = optionalRecoveryTimestamp(row, "last_success_at");
    recovery.retryCount = recoveryUInt64(row, "retry_count");
    recovery.lastError = optionalRecoveryString(row, "last_error");
}

}

std::shared_ptr<::vh::stats::model::VaultRecovery> Recovery::getVaultRecovery(const std::uint32_t vaultId) {
    return Transactions::exec("VaultRecovery::getVaultRecovery", [&](pqxx::work& txn) {
        auto recovery = std::make_shared<VaultRecovery>();
        recovery->vaultId = vaultId;
        recovery->checkedAt = recoveryUnixTimestamp();

        applyPolicy(*recovery, txn.exec(pqxx::prepped{"vault_recovery.policy"}, vaultId));
        recovery->finalize();

        return recovery;
    });
}

}
