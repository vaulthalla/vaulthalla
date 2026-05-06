#include "runtime/Manager.hpp"

#include "concurrency/AsyncService.hpp"
#include "db/Janitor.hpp"
#include "fuse/Service.hpp"
#include "log/Registry.hpp"
#include "log/RotationService.hpp"
#include "protocols/ProtocolService.hpp"
#include "protocols/shell/Server.hpp"
#include "protocols/ws/ConnectionLifecycleManager.hpp"
#include "runtime/Deps.hpp"
#include "stats/SnapshotService.hpp"
#include "sync/Controller.hpp"

#include <chrono>
#include <cstdlib>
#include <paths.h>
#include <utility>

namespace vh::runtime {

namespace {
constexpr auto kRestartDelay = std::chrono::milliseconds(500);
constexpr auto kWatchdogInterval = std::chrono::seconds(2);
}

Manager& Manager::instance() {
    static Manager instance;
    return instance;
}

Manager::Manager()
    : syncController(std::make_shared<sync::Controller>()),
      fuseService(std::make_shared<fuse::Service>()),
      protocolService(std::make_shared<protocols::ProtocolService>()),
      connectionLifecycleManager(std::make_shared<protocols::ws::ConnectionLifecycleManager>()),
      logRotationService(std::make_shared<log::RotationService>()),
      dbSweeperService(std::make_shared<db::Janitor>()),
      statsSnapshotService(std::make_shared<stats::SnapshotService>()) {

    services_["SyncController"] = syncController;
    services_["FUSE"] = fuseService;
    services_["ProtocolService"] = protocolService;
    services_["ConnectionLifecycleManager"] = connectionLifecycleManager;
    services_["LogRotationService"] = logRotationService;
    services_["DBJanitor"] = dbSweeperService;
    services_["StatsSnapshotService"] = statsSnapshotService;

    if (!paths::testMode) {
        shellServer = std::make_shared<protocols::shell::Server>();
        services_["ShellServer"] = shellServer;
    }
}

std::vector<Manager::ServiceEntry> Manager::serviceEntries() const {
    std::vector<ServiceEntry> entries;
    entries.reserve(services_.size());

    for (const auto& [name, service] : services_)
        entries.push_back({name, service});

    return entries;
}

void Manager::startAll() {
    log::Registry::runtime()->debug("[ServiceManager] Starting all services...");

    const auto entries = [&] {
        std::scoped_lock lock(mutex_);
        return serviceEntries();
    }();

    for (const auto& entry : entries) {
        tryStart(entry);
        if (entry.name == "FUSE" && fuseService)
            Deps::get().setFuseSession(fuseService->session());
    }

    startWatchdog();
    log::Registry::runtime()->debug("[ServiceManager] All services started.");
}

void Manager::startTestServices() {
    const auto entries = [&] {
        std::scoped_lock lock(mutex_);
        std::vector<ServiceEntry> selected;

        if (fuseService) selected.push_back({"FUSE", fuseService});
        if (shellServer) selected.push_back({"ShellServer", shellServer});

        return selected;
    }();

    for (const auto& entry : entries)
        tryStart(entry);
}

void Manager::stopAll(const int signal) {
    log::Registry::runtime()->debug("[ServiceManager] Stopping all services...");

    stopWatchdog();

    const auto entries = [&] {
        std::scoped_lock lock(mutex_);
        return serviceEntries();
    }();

    for (const auto& entry : entries)
        stopService(entry, signal);

    log::Registry::runtime()->debug("[ServiceManager] All services stopped.");
}

void Manager::restartService(const std::string& name) {
    std::shared_ptr<concurrency::AsyncService> service;

    {
        std::scoped_lock lock(mutex_);
        const auto it = services_.find(name);
        if (it == services_.end() || !it->second) return;
        service = it->second;
    }

    log::Registry::runtime()->warn("[ServiceManager] Restarting service: {}", name);

    stopService({name, service}, SIGTERM);
    std::this_thread::sleep_for(kRestartDelay);
    tryStart({name, service});

    if (name == "FUSE" && fuseService)
        Deps::get().setFuseSession(fuseService->session());
}

bool Manager::allRunning() const {
    const auto entries = [&] {
        std::scoped_lock lock(mutex_);
        return serviceEntries();
    }();

    for (const auto& entry : entries)
        if (!entry.service || !entry.service->isRunning())
            return false;

    return true;
}

Manager::Status Manager::status() const {
    const auto entries = [&] {
        std::scoped_lock lock(mutex_);
        return serviceEntries();
    }();

    Status out;
    out.allRunning = true;
    out.services.reserve(entries.size());

    for (const auto& entry : entries) {
        ServiceStatus s{};
        s.entryName = entry.name;

        if (entry.service) {
            const auto base = entry.service->status();
            s.serviceName = base.name;
            s.running = base.running;
            s.interrupted = base.interrupted;
            if (!base.running) out.allRunning = false;
        } else {
            s.serviceName = "uninitialized";
            s.running = false;
            s.interrupted = false;
            out.allRunning = false;
        }

        out.services.push_back(std::move(s));
    }

    if (out.services.empty()) out.allRunning = false;
    return out;
}

void Manager::tryStart(const ServiceEntry& entry) {
    if (!entry.service) return;

    log::Registry::runtime()->debug("[ServiceManager] Starting service: {}", entry.name);

    try {
        entry.service->start();
    } catch (const std::exception& e) {
        log::Registry::runtime()->error(
            "[ServiceManager] Failed to start {}: {}",
            entry.name,
            e.what()
        );
        hardFail();
    }
}

void Manager::stopService(const ServiceEntry& entry, const int signal) {
    if (!entry.service) return;

    log::Registry::runtime()->debug("[ServiceManager] Stopping service: {}", entry.name);

    try {
        entry.service->stop();
        log::Registry::runtime()->debug("[ServiceManager] Service stopped: {}", entry.name);
    } catch (...) {
        log::Registry::runtime()->error(
            "[ServiceManager] Failed to stop {} gracefully, escalating with signal {}.",
            entry.name,
            signal
        );
        std::raise(signal);
    }
}

void Manager::startWatchdog() {
    if (watchdogRunning.exchange(true)) return;

    watchdogThread = std::thread([this]() {
        log::Registry::runtime()->info("[ServiceManager] Watchdog started.");

        while (watchdogRunning) {
            std::vector<std::string> downServices;

            {
                std::scoped_lock lock(mutex_);
                for (const auto& [name, service] : services_)
                    if (service && !service->isRunning())
                        downServices.push_back(name);
            }

            for (const auto& name : downServices) {
                log::Registry::runtime()->warn("[Watchdog] {} is down, restarting...", name);

                if (name == "FUSE")
                    system(fmt::format(
                        "fusermount3 -u {} > /dev/null 2>&1 || fusermount -u {} > /dev/null 2>&1",
                        paths::getMountPath().string(),
                        paths::getMountPath().string()
                    ).c_str());

                restartService(name);
            }

            std::this_thread::sleep_for(kWatchdogInterval);
        }

        log::Registry::runtime()->info("[ServiceManager] Watchdog stopped.");
    });
}

void Manager::stopWatchdog() {
    if (!watchdogRunning.exchange(false)) return;
    if (watchdogThread.joinable()) watchdogThread.join();
}

[[noreturn]] void Manager::hardFail() {
    log::Registry::runtime()->error("[ServiceManager] Critical failure, cannot continue.");
    stopAll(SIGTERM);
    std::_Exit(EXIT_FAILURE);
}

}
