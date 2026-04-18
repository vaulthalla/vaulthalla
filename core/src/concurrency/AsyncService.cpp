#include "concurrency/AsyncService.hpp"
#include "log/Registry.hpp"

using namespace vh::concurrency;

AsyncService::AsyncService(const std::string& serviceName)
    : serviceName_(serviceName) {}

AsyncService::~AsyncService() {
    stop();
}

void AsyncService::start() {
    if (isRunning()) return;

    interruptFlag_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    worker_ = std::thread([this] {
        try {
            runLoop();
        } catch (const std::exception& e) {
            log::Registry::runtime()->error("[{}] Service error: {}", serviceName_, e.what());
        } catch (...) {
            log::Registry::runtime()->error("[{}] Service error: unknown exception.", serviceName_);
        }

        running_.store(false, std::memory_order_release);
    });

    log::Registry::runtime()->info("[{}] Service started.", serviceName_);
}

void AsyncService::stop() {
    if (!isRunning()) return;

    log::Registry::runtime()->info("[{}] Stopping service...", serviceName_);
    interruptFlag_.store(true, std::memory_order_release);

    if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id()) {
        worker_.join();
    }

    running_.store(false, std::memory_order_release);
    // Leave interruptFlag_ true until next start() resets it
    log::Registry::runtime()->info("[{}] Service stopped.", serviceName_);
}

void AsyncService::restart() {
    log::Registry::runtime()->info("[{}] Restarting service...", serviceName_);
    stop();
    start();
}
