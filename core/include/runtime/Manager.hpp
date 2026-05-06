#pragma once

#include "log/Registry.hpp"

#include <atomic>
#include <csignal>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vh::protocols {
class ProtocolService;
namespace shell { class Server; }
namespace ws { class ConnectionLifecycleManager; }
}

namespace vh::concurrency { class AsyncService; }
namespace vh::db { class Janitor; }
namespace vh::fuse { class Service; }
namespace vh::log { class RotationService; }
namespace vh::stats { class SnapshotService; }
namespace vh::sync { class Controller; }

namespace vh::runtime {

class Manager {
public:
    struct ServiceStatus {
        std::string entryName;
        std::string serviceName;
        bool running = false;
        bool interrupted = false;
    };

    struct Status {
        bool allRunning = false;
        std::vector<ServiceStatus> services;
    };

    static Manager& instance();

    void startAll();
    void stopAll(int signal = SIGTERM);
    void restartService(const std::string& name);
    void startTestServices();

    [[nodiscard]] bool allRunning() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] std::shared_ptr<sync::Controller> getSyncController() const { return syncController; }
    [[nodiscard]] std::shared_ptr<protocols::ProtocolService> getProtocolService() const { return protocolService; }
    [[nodiscard]] std::shared_ptr<protocols::shell::Server> getShellServer() const { return shellServer; }
    [[nodiscard]] std::shared_ptr<protocols::ws::ConnectionLifecycleManager> getConnectionLifecycleManager() const { return connectionLifecycleManager; }

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

private:
    struct ServiceEntry {
        std::string name;
        std::shared_ptr<concurrency::AsyncService> service;
    };

    Manager();

    [[nodiscard]] std::vector<ServiceEntry> serviceEntries() const;
    void tryStart(const ServiceEntry& entry);
    static void stopService(const ServiceEntry& entry, int signal);

    void startWatchdog();
    void stopWatchdog();
    [[noreturn]] void hardFail();

    std::shared_ptr<sync::Controller> syncController;
    std::shared_ptr<fuse::Service> fuseService;
    std::shared_ptr<protocols::ProtocolService> protocolService;
    std::shared_ptr<protocols::shell::Server> shellServer;
    std::shared_ptr<protocols::ws::ConnectionLifecycleManager> connectionLifecycleManager;
    std::shared_ptr<log::RotationService> logRotationService;
    std::shared_ptr<db::Janitor> dbSweeperService;
    std::shared_ptr<stats::SnapshotService> statsSnapshotService;

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<concurrency::AsyncService>> services_;

    std::thread watchdogThread;
    std::atomic<bool> watchdogRunning{false};
};

}
