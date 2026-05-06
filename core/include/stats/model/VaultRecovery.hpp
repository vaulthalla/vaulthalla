#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct VaultRecovery {
    std::uint32_t vaultId = 0;
    bool backupPolicyPresent = false;

    bool backupEnabled = false;
    std::string backupStatus = "unknown";
    std::uint64_t backupIntervalSeconds = 0;
    std::optional<std::uint64_t> retentionSeconds;
    std::optional<std::uint64_t> lastBackupAt;
    std::optional<std::uint64_t> lastSuccessAt;
    std::optional<std::uint64_t> lastSuccessAgeSeconds;
    std::optional<std::uint64_t> nextExpectedBackupAt;
    std::optional<bool> backupStale;
    std::optional<std::uint64_t> missedBackupCountEstimate;
    std::uint64_t retryCount = 0;
    std::optional<std::string> lastError;
    std::string recoveryReadiness = "unknown";
    std::optional<std::uint64_t> timeSinceLastVerifiedGoodState;
    std::uint64_t checkedAt = 0;

    void finalize();
};

void to_json(nlohmann::json& j, const VaultRecovery& recovery);

}
