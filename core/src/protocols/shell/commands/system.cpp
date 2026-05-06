#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/SystemHealth.hpp"
#include "usage/include/UsageManager.hpp"

#include <version.h>

#include <sstream>
#include <string>
#include <string_view>

namespace vh::protocols::shell::commands {

namespace {

CommandResult handleHelp(const CommandCall&) { return usage(); }

CommandResult handleVersion(const CommandCall&) {
    return {0, "Vaulthalla v" + std::string(VH_VERSION), ""};
}

std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

std::string renderDepsCoreReady(const stats::model::HealthSummary& summary) {
    std::ostringstream out;
    out << summary.depsReady << "/" << summary.depsTotal << " ready";
    return out.str();
}

CommandResult handleStatus(const CommandCall& call) {
    if (hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto health = stats::model::SystemHealth::snapshot();

    std::ostringstream out;
    out << "vh status: " << health.overallStatusString() << "\n";
    out << "runtime manager:\n";
    out << "  all services running: " << yesNo(health.runtime.allRunning) << "\n";
    out << "  service count: " << health.runtime.serviceCount << "\n";
    for (const auto& s : health.runtime.services) {
        out << "  - " << s.entryName << " (" << s.serviceName << "): "
            << (s.running ? "running" : "stopped");
        if (s.interrupted) out << " [interrupt requested]";
        out << "\n";
    }

    out << "protocol service:\n";
    out << "  running: " << yesNo(health.protocols.running) << "\n";
    out << "  io context initialized: " << yesNo(health.protocols.ioContextInitialized) << "\n";
    out << "  websocket: configured=" << yesNo(health.protocols.websocketConfigured)
        << ", ready=" << yesNo(health.protocols.websocketReady) << "\n";
    out << "  http preview: configured=" << yesNo(health.protocols.httpPreviewConfigured)
        << ", ready=" << yesNo(health.protocols.httpPreviewReady) << "\n";

    out << "deps sanity:\n";
    out << "  core deps: " << renderDepsCoreReady(health.summary) << "\n";
    out << "  fuse session: " << (health.deps.fuseSession ? "present" : "missing") << "\n";
    if (health.shell.adminUidBound) {
        out << "  shell admin uid bound: " << yesNo(*health.shell.adminUidBound);
        if (!*health.shell.adminUidBound) out << " (setup advisory)";
        out << "\n";
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
