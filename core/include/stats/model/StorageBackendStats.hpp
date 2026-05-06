#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct StorageVaultBackendStats {
    std::uint32_t vaultId = 0;
    std::string vaultName;
    std::string type = "unknown";
    bool active = false;
    bool allowFsWrite = false;
    std::uint64_t quotaBytes = 0;
    std::optional<std::uint64_t> freeSpaceBytes;
    std::optional<std::uint64_t> cacheSizeBytes;
    std::optional<std::uint64_t> vaultSizeBytes;
    std::string backendStatus = "unknown";
    std::optional<bool> minFreeSpaceOk;
    std::optional<bool> upstreamEncryptionEnabled;
    std::optional<std::string> bucket;
    std::optional<std::uint64_t> providerOpsTotal;
    std::optional<std::uint64_t> providerErrorsTotal;
    std::optional<double> providerErrorRate;
    std::optional<double> providerLatencyAvgMs;
    std::optional<double> providerLatencyMaxMs;
    std::optional<std::string> lastProviderError;
    std::optional<std::uint64_t> lastProviderSuccessAt;
};

struct StorageBackendStats {
    std::string overallStatus = "healthy";
    std::uint64_t vaultCountTotal = 0;
    std::uint64_t localVaultCount = 0;
    std::uint64_t s3VaultCount = 0;
    std::uint64_t activeVaultCount = 0;
    std::uint64_t inactiveVaultCount = 0;
    std::uint64_t degradedVaultCount = 0;
    std::uint64_t errorVaultCount = 0;
    std::vector<StorageVaultBackendStats> vaults;
    std::uint64_t checkedAt = 0;

    static StorageBackendStats snapshot();
    static StorageBackendStats snapshotForVault(std::uint32_t vaultId);
    void finalize();
};

void to_json(nlohmann::json& j, const StorageVaultBackendStats& stats);
void to_json(nlohmann::json& j, const StorageBackendStats& stats);

}
