#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Server.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/commandHelpers.hpp"
#include "protocols/ProtocolService.hpp"
#include "runtime/Manager.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <version.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <string_view>

namespace vh::protocols::shell::commands {

namespace system_cmd_util = vh::protocols::shell::util;

namespace {

CommandResult handleHelp(const CommandCall&) { return usage(); }

CommandResult handleVersion(const CommandCall&) {
    return {0, "Vaulthalla v" + std::string(VH_VERSION), ""};
}

std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

std::string renderDepsCoreReady(const runtime::Deps::SanityStatus& deps, size_t& ready, size_t& total) {
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
    total = checks.size();
    ready = static_cast<size_t>(std::ranges::count(checks, true));
    std::ostringstream out;
    out << ready << "/" << total << " ready";
    return out.str();
}

std::string systemdUnitState(const std::string& unit) {
    const auto state = system_cmd_util::runCapture("systemctl is-active " + system_cmd_util::shellQuote(unit));
    if (state.code == 0 && !state.output.empty()) return state.output;
    if (!state.output.empty()) return state.output;
    return "unknown (exit " + std::to_string(state.code) + ")";
}

CommandResult handleStatus(const CommandCall& call) {
    if (hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto& manager = runtime::Manager::instance();
    const auto runtimeStatus = manager.status();
    const auto protocolService = manager.getProtocolService();
    const auto shellServer = manager.getShellServer();
    const auto protocolStatus = protocolService ? protocolService->protocolStatus() : protocols::ProtocolService::RuntimeStatus{};
    const auto depsStatus = runtime::Deps::get().sanityStatus();

    size_t depsReady = 0;
    size_t depsTotal = 0;
    const auto depsReadySummary = renderDepsCoreReady(depsStatus, depsReady, depsTotal);

    const bool protocolHealthy = !protocolService
        ? false
        : (!protocolStatus.websocketConfigured || protocolStatus.websocketReady) &&
          (!protocolStatus.httpPreviewConfigured || protocolStatus.httpPreviewReady);

    const bool depsHealthy = depsReady == depsTotal;
    const bool overallHealthy = runtimeStatus.allRunning && protocolHealthy && depsHealthy;

    std::ostringstream out;
    out << "vh status: " << (overallHealthy ? "healthy" : "degraded") << "\n";
    out << "runtime manager:\n";
    out << "  all services running: " << yesNo(runtimeStatus.allRunning) << "\n";
    out << "  service count: " << runtimeStatus.services.size() << "\n";
    for (const auto& s : runtimeStatus.services) {
        out << "  - " << s.entryName << " (" << s.serviceName << "): "
            << (s.running ? "running" : "stopped");
        if (s.interrupted) out << " [interrupt requested]";
        out << "\n";
    }

    out << "protocol service:\n";
    out << "  running: " << yesNo(protocolStatus.running) << "\n";
    out << "  io context initialized: " << yesNo(protocolStatus.ioContextInitialized) << "\n";
    out << "  websocket: configured=" << yesNo(protocolStatus.websocketConfigured)
        << ", ready=" << yesNo(protocolStatus.websocketReady) << "\n";
    out << "  http preview: configured=" << yesNo(protocolStatus.httpPreviewConfigured)
        << ", ready=" << yesNo(protocolStatus.httpPreviewReady) << "\n";

    out << "deps sanity:\n";
    out << "  core deps: " << depsReadySummary << "\n";
    out << "  fuse session: " << (depsStatus.fuseSession ? "present" : "missing") << "\n";
    if (shellServer)
        out << "  shell admin uid bound: " << yesNo(shellServer->adminUIDSet()) << "\n";

    if (system_cmd_util::hasEffectiveRoot() && system_cmd_util::commandExists("systemctl")) {
        out << "systemd summary (supplemental):\n";
        constexpr std::array<std::string_view, 4> units{
            "vaulthalla.service",
            "vaulthalla-cli.service",
            "vaulthalla-cli.socket",
            "vaulthalla-web.service"
        };
        for (const auto unit : units)
            out << "  " << unit << ": " << systemdUnitState(std::string(unit)) << "\n";
    } else {
        out << "systemd summary: unavailable (run with elevated privileges for unit state)\n";
    }

    return ok(out.str());
}

}

void registerSystemCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("help"), handleHelp);
    r->registerCommand(usageManager->resolve("version"), handleVersion);
}

void registerStatusCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("status"), handleStatus);
}

}
