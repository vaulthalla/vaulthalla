#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/SystemHealth.hpp"
#include "db/DBPool.hpp"
#include "db/Transactions.hpp"
#include "usage/include/UsageManager.hpp"

#include <version.h>

#include <chrono>
#include <pqxx/pqxx>
#include <sstream>
#include <string>
#include <string_view>

namespace vh::protocols::shell::commands {

namespace {

CommandResult handleHelp(const CommandCall&) { return usage(); }

CommandResult handleVersion(const CommandCall&) {
    return {0, "Vaulthalla v" + std::string(VH_VERSION), ""};
}

// Bounded live round-trip to PostgreSQL. The pool counters only reflect failures somebody already hit; a
// status check must find out now, and must not park on the pool if every connection is busy.
struct DatabaseProbe {
    bool ok = false;
    long long latencyMs = 0;
    std::string error;
};

constexpr std::chrono::seconds kDatabaseProbeAcquireTimeout{3};

DatabaseProbe probeDatabase() {
    DatabaseProbe out;
    const auto pool = db::Transactions::dbPool_;
    if (!pool) {
        out.error = "database pool is not initialized";
        return out;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto elapsedMs = [&start] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };

    try {
        auto lease = pool->acquire(kDatabaseProbeAcquireTimeout);
        try {
            pqxx::nontransaction tx(lease->get());
            (void)tx.exec("SELECT 1").one_row();
        } catch (const std::exception&) {
            // A session the server dropped while idle only shows up when used; replace it once, like
            // Transactions::exec does, so a recovered server reads as healthy.
            if (lease->healthy()) throw;
            pool->repair(lease);
            pqxx::nontransaction tx(lease->get());
            (void)tx.exec("SELECT 1").one_row();
        }
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    out.latencyMs = elapsedMs();
    return out;
}

// healthy -> 0, degraded -> 1, critical -> 2 (monitoring-plugin convention), so scripts can act on `vh status`.
int statusExitCode(const stats::model::SystemHealthStatus status) {
    switch (status) {
        case stats::model::SystemHealthStatus::Healthy: return 0;
        case stats::model::SystemHealthStatus::Critical: return 2;
        case stats::model::SystemHealthStatus::Degraded:
        default: return 1;
    }
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
    const auto dbProbe = probeDatabase();

    // An unreachable database makes the whole system critical, whatever the in-process services say.
    const auto overall = dbProbe.ok ? health.overallStatus : stats::model::SystemHealthStatus::Critical;

    std::ostringstream out;
    out << "vh status: " << stats::model::to_string(overall) << "\n";
    out << "database:\n";
    if (dbProbe.ok) out << "  reachable: yes (" << dbProbe.latencyMs << " ms round trip)\n";
    else out << "  reachable: NO (" << dbProbe.error << ", after " << dbProbe.latencyMs << " ms)\n";
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

    out << "s3 gateway:\n";
    out << "  running: " << yesNo(health.s3Gateway.running) << "\n";
    out << "  configured: " << yesNo(health.s3Gateway.configured)
        << ", ready=" << yesNo(health.s3Gateway.ready) << "\n";
    out << "  endpoint: " << health.s3Gateway.host << ":" << health.s3Gateway.port << "\n";
    out << "  sessions: active=" << health.s3Gateway.activeSessions
        << ", requests=" << health.s3Gateway.totalRequests
        << ", failed=" << health.s3Gateway.failedRequests << "\n";

    if (health.database) {
        const auto& pool = *health.database;
        out << "database pool:\n";
        out << "  connections: size=" << pool.poolSize << ", idle=" << pool.idle << ", in use=" << pool.inUse
            << ", broken idle=" << pool.brokenIdle << "\n";
        out << "  reconnects: ok=" << pool.reconnects << ", failed=" << pool.reconnectFailures
            << " (consecutive=" << pool.consecutiveReconnectFailures << ")"
            << ", acquire timeouts=" << pool.acquireTimeouts << "\n";
    }

    out << "deps sanity:\n";
    out << "  core deps: " << renderDepsCoreReady(health.summary) << "\n";
    out << "  fuse session: " << (health.deps.fuseSession ? "present" : "missing") << "\n";
    if (health.shell.adminUidBound) {
        out << "  shell admin uid bound: " << yesNo(*health.shell.adminUidBound);
        if (!*health.shell.adminUidBound) out << " (setup advisory)";
        out << "\n";
    }

    return {statusExitCode(overall), out.str(), ""};
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
