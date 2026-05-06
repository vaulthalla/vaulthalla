#include "stats/model/StorageBackendStats.hpp"

#include "fs/model/Path.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/Vault.hpp"

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableStorageValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

std::uint64_t storageUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string vaultTypeName(const vh::vault::model::VaultType type) {
    return type == vh::vault::model::VaultType::S3 ? "s3" : "local";
}

std::uint64_t safeAvailableSpace(const std::shared_ptr<storage::Engine>& engine) {
    if (!engine || !engine->paths) return 0;
    std::error_code ec;
    const auto info = std::filesystem::space(engine->paths->backingRoot, ec);
    return ec ? 0 : static_cast<std::uint64_t>(info.available);
}

StorageVaultBackendStats snapshotVault(const std::shared_ptr<storage::Engine>& engine) {
    StorageVaultBackendStats out;

    if (!engine || !engine->vault) {
        out.backendStatus = "error";
        return out;
    }

    const auto& vault = engine->vault;
    out.vaultId = vault->id;
    out.vaultName = vault->name;
    out.type = vaultTypeName(vault->type);
    out.active = vault->is_active;
    out.allowFsWrite = vault->allow_fs_write;
    out.quotaBytes = static_cast<std::uint64_t>(vault->quota);

    if (vault->type == vh::vault::model::VaultType::S3) {
        const auto s3Vault = std::dynamic_pointer_cast<vh::vault::model::S3Vault>(vault);
        if (s3Vault) {
            out.upstreamEncryptionEnabled = s3Vault->encrypt_upstream;
            out.bucket = s3Vault->bucket.empty() ? std::nullopt : std::optional<std::string>(s3Vault->bucket);
        }
    }

    try {
        const auto vaultSize = static_cast<std::uint64_t>(engine->getVaultSize());
        const auto cacheSize = static_cast<std::uint64_t>(engine->getCacheSize());
        out.vaultSizeBytes = vaultSize;
        out.cacheSizeBytes = cacheSize;

        if (out.quotaBytes > 0) {
            const auto usedWithReserve = vaultSize + cacheSize + storage::Engine::MIN_FREE_SPACE;
            out.freeSpaceBytes = out.quotaBytes > usedWithReserve ? out.quotaBytes - usedWithReserve : 0;
            out.minFreeSpaceOk = out.quotaBytes > usedWithReserve;
        } else {
            const auto available = safeAvailableSpace(engine);
            out.freeSpaceBytes = available;
            out.minFreeSpaceOk = available >= storage::Engine::MIN_FREE_SPACE;
        }
    } catch (const std::exception& e) {
        out.backendStatus = "error";
        out.lastProviderError = e.what();
        return out;
    }

    if (!out.active || (out.minFreeSpaceOk && !*out.minFreeSpaceOk)) {
        out.backendStatus = "degraded";
    } else if (out.type == "s3" && (!out.bucket || out.bucket->empty())) {
        out.backendStatus = "degraded";
    } else {
        out.backendStatus = "healthy";
    }

    return out;
}

StorageVaultBackendStats snapshotMissingVault(const std::uint32_t vaultId) {
    StorageVaultBackendStats out;
    out.vaultId = vaultId;
    out.backendStatus = "error";
    return out;
}

}

StorageBackendStats StorageBackendStats::snapshot() {
    StorageBackendStats out;
    out.checkedAt = storageUnixTimestamp();

    const auto manager = runtime::Deps::get().storageManager;
    if (!manager) {
        out.overallStatus = "unknown";
        return out;
    }

    for (const auto& engine : manager->getEngines())
        out.vaults.push_back(snapshotVault(engine));

    out.finalize();
    return out;
}

StorageBackendStats StorageBackendStats::snapshotForVault(const std::uint32_t vaultId) {
    StorageBackendStats out;
    out.checkedAt = storageUnixTimestamp();

    const auto manager = runtime::Deps::get().storageManager;
    if (!manager) {
        out.overallStatus = "unknown";
        return out;
    }

    const auto engine = manager->getEngine(vaultId);
    out.vaults.push_back(engine ? snapshotVault(engine) : snapshotMissingVault(vaultId));
    out.finalize();
    return out;
}

void StorageBackendStats::finalize() {
    vaultCountTotal = vaults.size();
    localVaultCount = 0;
    s3VaultCount = 0;
    activeVaultCount = 0;
    inactiveVaultCount = 0;
    degradedVaultCount = 0;
    errorVaultCount = 0;

    for (const auto& vault : vaults) {
        if (vault.type == "s3") ++s3VaultCount;
        else if (vault.type == "local") ++localVaultCount;

        if (vault.active) ++activeVaultCount;
        else ++inactiveVaultCount;

        if (vault.backendStatus == "error") ++errorVaultCount;
        else if (vault.backendStatus == "degraded") ++degradedVaultCount;
    }

    if (vaultCountTotal == 0) overallStatus = "unknown";
    else if (errorVaultCount > 0) overallStatus = "error";
    else if (degradedVaultCount > 0) overallStatus = "degraded";
    else overallStatus = "healthy";
}

void to_json(nlohmann::json& j, const StorageVaultBackendStats& stats) {
    j = nlohmann::json{
        {"vault_id", stats.vaultId},
        {"vault_name", stats.vaultName},
        {"type", stats.type},
        {"active", stats.active},
        {"allow_fs_write", stats.allowFsWrite},
        {"quota_bytes", stats.quotaBytes},
        {"free_space_bytes", nullableStorageValue(stats.freeSpaceBytes)},
        {"cache_size_bytes", nullableStorageValue(stats.cacheSizeBytes)},
        {"vault_size_bytes", nullableStorageValue(stats.vaultSizeBytes)},
        {"backend_status", stats.backendStatus},
        {"min_free_space_ok", nullableStorageValue(stats.minFreeSpaceOk)},
        {"upstream_encryption_enabled", nullableStorageValue(stats.upstreamEncryptionEnabled)},
        {"bucket", nullableStorageValue(stats.bucket)},
        {"provider_ops_total", nullableStorageValue(stats.providerOpsTotal)},
        {"provider_errors_total", nullableStorageValue(stats.providerErrorsTotal)},
        {"provider_error_rate", nullableStorageValue(stats.providerErrorRate)},
        {"provider_latency_avg_ms", nullableStorageValue(stats.providerLatencyAvgMs)},
        {"provider_latency_max_ms", nullableStorageValue(stats.providerLatencyMaxMs)},
        {"last_provider_error", nullableStorageValue(stats.lastProviderError)},
        {"last_provider_success_at", nullableStorageValue(stats.lastProviderSuccessAt)},
    };
}

void to_json(nlohmann::json& j, const StorageBackendStats& stats) {
    j = nlohmann::json{
        {"overall_status", stats.overallStatus},
        {"vault_count_total", stats.vaultCountTotal},
        {"local_vault_count", stats.localVaultCount},
        {"s3_vault_count", stats.s3VaultCount},
        {"active_vault_count", stats.activeVaultCount},
        {"inactive_vault_count", stats.inactiveVaultCount},
        {"degraded_vault_count", stats.degradedVaultCount},
        {"error_vault_count", stats.errorVaultCount},
        {"vaults", stats.vaults},
        {"checked_at", stats.checkedAt},
    };
}

}
