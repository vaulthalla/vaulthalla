#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vh::concurrency {

class AsyncService {
public:
    struct Status {
        std::string name;
        bool running = false;
        bool interrupted = false;
    };

    explicit AsyncService(const std::string& serviceName);
    virtual ~AsyncService();

    virtual void start();
    // requestStop() + join(). Manager stops services in two phases (request all, then join each) so shutdown
    // takes as long as the slowest service, not the sum of all of them.
    virtual void stop();
    virtual void restart();

    // Sets the interrupt flag, wakes lazySleep() and runs onStop(). Never blocks; safe to call more than once.
    void requestStop();
    // Joins the worker thread (after requestStop()).
    void join();

    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] virtual Status status() const;
    [[nodiscard]] const std::string& serviceName() const noexcept { return serviceName_; }

protected:
    std::string serviceName_;
    std::atomic<bool> running_{false};
    std::atomic<bool> interruptFlag_{false};
    std::thread worker_;

    virtual void runLoop() = 0;
    // Optional stop hook for derived services to unblock blocking syscalls before join().
    virtual void onStop() {}

    [[nodiscard]] bool shouldStop() const {
        return interruptFlag_.load(std::memory_order_relaxed) ||
               !running_.load(std::memory_order_relaxed);
    }

    // Sleeps for up to `total`; returns true if it slept the full duration, false as soon as a stop is requested.
    template <class Rep, class Period>
    bool lazySleep(const std::chrono::duration<Rep, Period> total) {
        if (total <= total.zero()) return !shouldStop();
        std::unique_lock lock(stopMutex_);
        return !stopCv_.wait_for(lock, total, [this] { return shouldStop(); });
    }

private:
    std::mutex stopMutex_;
    std::condition_variable stopCv_;
};

}
