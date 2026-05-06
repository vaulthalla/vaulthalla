#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::sync::model { struct Event; }

namespace vh::stats::model {

enum class VaultSyncOverallStatus {
    Healthy,
    Syncing,
    Warning,
    Stalled,
    Error,
    Diverged,
    Unknown,
};

enum class VaultSyncCurrentState {
    Idle,
    Pending,
    Running,
    Stalled,
    Error,
    Cancelled,
    Success,
    Diverged,
    Unknown,
};

struct VaultSyncHealth {
    std::uint32_t vaultId = 0;

    VaultSyncOverallStatus overallStatus = VaultSyncOverallStatus::Unknown;
    VaultSyncCurrentState currentState = VaultSyncCurrentState::Unknown;

    bool syncEnabled = false;
    std::uint64_t syncIntervalSeconds = 0;
    std::optional<std::string> configuredStrategy;
    std::optional<std::string> conflictPolicy;

    std::shared_ptr<sync::model::Event> latestEvent;
    std::optional<std::uint32_t> latestEventId;
    std::optional<std::string> latestRunUuid;

    std::optional<std::uint64_t> lastSyncAt;
    std::optional<std::uint64_t> lastSuccessAt;
    std::optional<std::uint64_t> lastSuccessAgeSeconds;

    std::uint64_t activeRunCount = 0;
    std::uint64_t pendingRunCount = 0;
    std::uint64_t runningRunCount = 0;
    std::uint64_t stalledRunCount = 0;

    std::optional<std::uint64_t> oldestActiveAgeSeconds;
    std::optional<std::uint64_t> latestHeartbeatAgeSeconds;

    std::uint64_t errorCount24h = 0;
    std::uint64_t errorCount7d = 0;
    std::uint64_t failedOps24h = 0;
    std::uint64_t failedOps7d = 0;

    std::uint64_t retryCount24h = 0;
    std::uint64_t retryCount7d = 0;

    std::uint64_t conflictCountOpen = 0;
    std::uint64_t conflictCount24h = 0;
    std::uint64_t conflictCount7d = 0;

    std::uint64_t bytesUp24h = 0;
    std::uint64_t bytesDown24h = 0;
    std::uint64_t bytesTotal24h = 0;

    std::uint64_t bytesUp7d = 0;
    std::uint64_t bytesDown7d = 0;
    std::uint64_t bytesTotal7d = 0;

    double avgThroughputBytesPerSec24h = 0.0;
    double peakThroughputBytesPerSec24h = 0.0;

    bool divergenceDetected = false;
    std::optional<std::string> localStateHash;
    std::optional<std::string> remoteStateHash;
    bool hashMismatch = false;

    std::optional<std::string> lastErrorCode;
    std::optional<std::string> lastErrorMessage;
    std::optional<std::string> lastStallReason;

    std::uint64_t checkedAt = 0;

    bool syncConfigPresent = false;
    bool syncHistoryPresent = false;
    bool lastErrorAfterLastSuccess = false;

    void finalize();
};

std::string to_string(VaultSyncOverallStatus status);
std::string to_string(VaultSyncCurrentState state);

void to_json(nlohmann::json& j, const VaultSyncHealth& health);
void to_json(nlohmann::json& j, const std::shared_ptr<VaultSyncHealth>& health);

}
