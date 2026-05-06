#include "stats/model/SystemHealth.hpp"

#include "protocols/ProtocolService.hpp"
#include "protocols/shell/Server.hpp"
#include "runtime/Deps.hpp"
#include "runtime/Manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

std::uint64_t unixTimestamp() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(now));
}

std::size_t countReadyDeps(const DependencyHealth& deps) {
    const std::array<bool, 9> checks{
        deps.storageManager,
        deps.apiKeyManager,
        deps.authManager,
        deps.sessionManager,
        deps.secretsManager,
        deps.syncController,
        deps.fsCache,
        deps.shellUsageManager,
        deps.httpCacheStats
    };

    return static_cast<std::size_t>(std::ranges::count(checks, true));
}

bool depsHealthy(const DependencyHealth& deps, const HealthSummary& summary) {
    return summary.depsReady == summary.depsTotal && deps.fuseSession;
}

bool protocolsHealthy(const ProtocolHealth& protocols) {
    return (!protocols.websocketConfigured || protocols.websocketReady)
        && (!protocols.httpPreviewConfigured || protocols.httpPreviewReady);
}

std::pair<std::size_t, std::size_t> protocolReadySummary(const ProtocolHealth& protocols) {
    std::size_t ready = 0;
    std::size_t total = 0;

    if (protocols.websocketConfigured) {
        ++total;
        if (protocols.websocketReady) ++ready;
    }

    if (protocols.httpPreviewConfigured) {
        ++total;
        if (protocols.httpPreviewReady) ++ready;
    }

    return {ready, total};
}

SystemHealthStatus computeOverallStatus(const SystemHealth& health) {
    if (health.runtime.services.empty())
        return SystemHealthStatus::Critical;

    if (!health.deps.storageManager && !health.deps.authManager && !health.deps.sessionManager)
        return SystemHealthStatus::Critical;

    const bool ok = health.runtime.allRunning
        && protocolsHealthy(health.protocols)
        && depsHealthy(health.deps, health.summary);

    return ok ? SystemHealthStatus::Healthy : SystemHealthStatus::Degraded;
}

}

bool SystemHealth::healthy() const noexcept {
    return overallStatus == SystemHealthStatus::Healthy;
}

std::string SystemHealth::overallStatusString() const {
    return to_string(overallStatus);
}

SystemHealth SystemHealth::snapshot() {
    const auto& manager = runtime::Manager::instance();
    const auto runtimeStatus = manager.status();
    const auto protocolService = manager.getProtocolService();
    const auto shellServer = manager.getShellServer();
    const auto protocolStatus = protocolService
        ? protocolService->protocolStatus()
        : protocols::ProtocolService::RuntimeStatus{};
    const auto depsStatus = runtime::Deps::get().sanityStatus();

    SystemHealth out;
    out.runtime.allRunning = runtimeStatus.allRunning;
    out.runtime.serviceCount = runtimeStatus.services.size();
    out.runtime.services.reserve(runtimeStatus.services.size());

    for (const auto& service : runtimeStatus.services) {
        out.runtime.services.push_back({
            .entryName = service.entryName,
            .serviceName = service.serviceName,
            .running = service.running,
            .interrupted = service.interrupted
        });
    }

    out.protocols = {
        .running = protocolStatus.running,
        .ioContextInitialized = protocolStatus.ioContextInitialized,
        .websocketConfigured = protocolStatus.websocketConfigured,
        .websocketReady = protocolStatus.websocketReady,
        .httpPreviewConfigured = protocolStatus.httpPreviewConfigured,
        .httpPreviewReady = protocolStatus.httpPreviewReady
    };

    out.deps = {
        .storageManager = depsStatus.storageManager,
        .apiKeyManager = depsStatus.apiKeyManager,
        .authManager = depsStatus.authManager,
        .sessionManager = depsStatus.sessionManager,
        .secretsManager = depsStatus.secretsManager,
        .syncController = depsStatus.syncController,
        .fsCache = depsStatus.fsCache,
        .shellUsageManager = depsStatus.shellUsageManager,
        .httpCacheStats = depsStatus.httpCacheStats,
        .fuseSession = depsStatus.fuseSession
    };

    out.shell.adminUidBound = shellServer ? std::optional<bool>(shellServer->adminUIDSet()) : std::nullopt;

    const auto [protocolsReady, protocolsTotal] = protocolReadySummary(out.protocols);
    out.summary = {
        .servicesReady = static_cast<std::size_t>(std::ranges::count_if(
            out.runtime.services,
            [](const RuntimeServiceHealth& service) { return service.running; }
        )),
        .servicesTotal = out.runtime.services.size(),
        .depsReady = countReadyDeps(out.deps),
        .depsTotal = 9,
        .protocolsReady = protocolsReady,
        .protocolsTotal = protocolsTotal,
        .checkedAt = unixTimestamp()
    };

    out.overallStatus = computeOverallStatus(out);
    return out;
}

std::string to_string(const SystemHealthStatus status) {
    switch (status) {
        case SystemHealthStatus::Healthy:
            return "healthy";
        case SystemHealthStatus::Critical:
            return "critical";
        case SystemHealthStatus::Degraded:
        default:
            return "degraded";
    }
}

void to_json(nlohmann::json& j, const RuntimeServiceHealth& health) {
    j = nlohmann::json{
        {"entry_name", health.entryName},
        {"service_name", health.serviceName},
        {"running", health.running},
        {"interrupted", health.interrupted},
    };
}

void to_json(nlohmann::json& j, const RuntimeHealth& health) {
    j = nlohmann::json{
        {"all_running", health.allRunning},
        {"service_count", health.serviceCount},
        {"services", health.services},
    };
}

void to_json(nlohmann::json& j, const ProtocolHealth& health) {
    j = nlohmann::json{
        {"running", health.running},
        {"io_context_initialized", health.ioContextInitialized},
        {"websocket_configured", health.websocketConfigured},
        {"websocket_ready", health.websocketReady},
        {"http_preview_configured", health.httpPreviewConfigured},
        {"http_preview_ready", health.httpPreviewReady},
    };
}

void to_json(nlohmann::json& j, const DependencyHealth& health) {
    j = nlohmann::json{
        {"storage_manager", health.storageManager},
        {"api_key_manager", health.apiKeyManager},
        {"auth_manager", health.authManager},
        {"session_manager", health.sessionManager},
        {"secrets_manager", health.secretsManager},
        {"sync_controller", health.syncController},
        {"fs_cache", health.fsCache},
        {"shell_usage_manager", health.shellUsageManager},
        {"http_cache_stats", health.httpCacheStats},
        {"fuse_session", health.fuseSession},
    };
}

void to_json(nlohmann::json& j, const ShellHealth& health) {
    j = nlohmann::json{
        {"admin_uid_bound", health.adminUidBound ? nlohmann::json(*health.adminUidBound) : nlohmann::json(nullptr)},
    };
}

void to_json(nlohmann::json& j, const HealthSummary& health) {
    j = nlohmann::json{
        {"services_ready", health.servicesReady},
        {"services_total", health.servicesTotal},
        {"deps_ready", health.depsReady},
        {"deps_total", health.depsTotal},
        {"protocols_ready", health.protocolsReady},
        {"protocols_total", health.protocolsTotal},
        {"checked_at", health.checkedAt},
    };
}

void to_json(nlohmann::json& j, const SystemHealth& health) {
    j = nlohmann::json{
        {"overall_status", to_string(health.overallStatus)},
        {"runtime", health.runtime},
        {"protocols", health.protocols},
        {"deps", health.deps},
        {"shell", health.shell},
        {"summary", health.summary},
    };
}

}
