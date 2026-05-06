#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

enum class SystemHealthStatus {
    Healthy,
    Degraded,
    Critical,
};

struct RuntimeServiceHealth {
    std::string entryName;
    std::string serviceName;
    bool running = false;
    bool interrupted = false;
};

struct RuntimeHealth {
    bool allRunning = false;
    std::size_t serviceCount = 0;
    std::vector<RuntimeServiceHealth> services;
};

struct ProtocolHealth {
    bool running = false;
    bool ioContextInitialized = false;
    bool websocketConfigured = false;
    bool websocketReady = false;
    bool httpPreviewConfigured = false;
    bool httpPreviewReady = false;
};

struct DependencyHealth {
    bool storageManager = false;
    bool apiKeyManager = false;
    bool authManager = false;
    bool sessionManager = false;
    bool secretsManager = false;
    bool syncController = false;
    bool fsCache = false;
    bool shellUsageManager = false;
    bool httpCacheStats = false;
    bool fuseSession = false;
};

struct ShellHealth {
    std::optional<bool> adminUidBound;
};

struct HealthSummary {
    std::size_t servicesReady = 0;
    std::size_t servicesTotal = 0;
    std::size_t depsReady = 0;
    std::size_t depsTotal = 0;
    std::size_t protocolsReady = 0;
    std::size_t protocolsTotal = 0;
    std::uint64_t checkedAt = 0;
};

struct SystemHealth {
    SystemHealthStatus overallStatus = SystemHealthStatus::Degraded;
    RuntimeHealth runtime;
    ProtocolHealth protocols;
    DependencyHealth deps;
    ShellHealth shell;
    HealthSummary summary;

    [[nodiscard]] bool healthy() const noexcept;
    [[nodiscard]] std::string overallStatusString() const;

    static SystemHealth snapshot();
};

std::string to_string(SystemHealthStatus status);

void to_json(nlohmann::json& j, const RuntimeServiceHealth& health);
void to_json(nlohmann::json& j, const RuntimeHealth& health);
void to_json(nlohmann::json& j, const ProtocolHealth& health);
void to_json(nlohmann::json& j, const DependencyHealth& health);
void to_json(nlohmann::json& j, const ShellHealth& health);
void to_json(nlohmann::json& j, const HealthSummary& health);
void to_json(nlohmann::json& j, const SystemHealth& health);

}
