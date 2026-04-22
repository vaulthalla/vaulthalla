#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Server.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/ProtocolService.hpp"
#include "runtime/Manager.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace vh;
using namespace vh::protocols::shell;

static CommandResult handle_help(const CommandCall&) { return usage(); }

static CommandResult handle_version(const CommandCall&) {
    return {0, "Vaulthalla v" + std::string(VH_VERSION), ""};
}

namespace {

struct ExecResult {
    int code = 1;
    std::string output;
};

static std::string trim(std::string s) {
    const auto ws = [](const unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](const unsigned char c) { return !ws(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](const unsigned char c) { return !ws(c); }).base(), s.end());
    return s;
}

static std::string shellQuote(const std::string& s) {
    std::string out{"'"};
    for (const char c : s) {
        if (c == '\'') out += "'\"'\"'";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static ExecResult runCapture(const std::string& command) {
    const auto wrapped = command + " 2>&1";
    std::array<char, 4096> buf{};
    std::string output;

    FILE* pipe = ::popen(wrapped.c_str(), "r");
    if (!pipe) return {.code = 1, .output = "failed to execute command"};

    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        output += buf.data();

    const int status = ::pclose(pipe);
    int code = status;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);

    return {.code = code, .output = trim(output)};
}

static bool commandExists(const std::string& command) {
    return runCapture("command -v " + shellQuote(command) + " >/dev/null").code == 0;
}

static bool hasEffectiveRoot() {
    return ::geteuid() == 0;
}

static std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

static std::string renderDepsCoreReady(const runtime::Deps::SanityStatus& deps, size_t& ready, size_t& total) {
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

static std::string systemdUnitState(const std::string& unit) {
    const auto state = runCapture("systemctl is-active " + shellQuote(unit));
    if (state.code == 0 && !state.output.empty()) return state.output;
    if (!state.output.empty()) return state.output;
    return "unknown (exit " + std::to_string(state.code) + ")";
}

static CommandResult handle_status(const CommandCall& call) {
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

    if (hasEffectiveRoot() && commandExists("systemctl")) {
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

} // namespace

void commands::registerSystemCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("help"), handle_help);
    r->registerCommand(usageManager->resolve("version"), handle_version);
}

void commands::registerStatusCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("status"), handle_status);
}
