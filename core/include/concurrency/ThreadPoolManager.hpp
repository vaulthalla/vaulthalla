#pragma once

#include "concurrency/ThreadPool.hpp"
#include "log/Registry.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace vh::concurrency {

class ThreadPoolManager {
public:
    static ThreadPoolManager& instance() {
        static ThreadPoolManager instance;
        return instance;
    }

    void init() {
        if (running_.exchange(true)) return; // already running

        totalThreads_ = std::max(
            std::thread::hardware_concurrency() * RESERVE_FACTOR,
            8u // safety floor
        );

        const unsigned int base = totalThreads_ / NUM_POOLS;
        const unsigned int rem  = totalThreads_ % NUM_POOLS;

        unsigned int fuseN  = base + (rem > 0 ? 1 : 0);
        unsigned int syncN  = base + (rem > 1 ? 1 : 0);
        unsigned int thumbN = base; // thumb/http get no extra by default
        unsigned int httpN  = base;
        unsigned int statsN = base;

        fuse_  = std::make_shared<ThreadPool>(nullptr, fuseN);
        sync_  = std::make_shared<ThreadPool>(nullptr, syncN);
        thumb_ = std::make_shared<ThreadPool>(nullptr, thumbN);
        http_  = std::make_shared<ThreadPool>(nullptr, httpN);
        stats_ = std::make_shared<ThreadPool>(nullptr, statsN);

        stopFlag_.store(false);

        monitorThread_ = std::thread([this] { rebalanceLoop(); });
    }

    void shutdown() {
        stopFlag_.store(true);
        {
            std::scoped_lock lock(pressureMutex_);
            pressureFlag_.store(true); // optional, just to trip the predicate
        }
        pressureCv_.notify_all();  // wake the monitor

        log::Registry::runtime()->debug("[ThreadPoolManager] Waiting for monitor thread to finish...");
        if (monitorThread_.joinable()) monitorThread_.join();

        log::Registry::runtime()->info("[ThreadPoolManager] Stopping thread pools...");
        if (fuse_)  fuse_->stop();
        if (sync_)  sync_->stop();
        if (thumb_) thumb_->stop();
        if (http_)  http_->stop();
        if (stats_) stats_->stop();

        running_.store(false);
    }

    std::shared_ptr<ThreadPool>& fusePool() { return fuse_; }
    std::shared_ptr<ThreadPool>& syncPool() { return sync_; }
    std::shared_ptr<ThreadPool>& thumbPool() { return thumb_; }
    std::shared_ptr<ThreadPool>& httpPool() { return http_; }
    std::shared_ptr<ThreadPool>& statsPool() { return stats_; }

    void signalPressureChange() {
        {
            std::scoped_lock lock(pressureMutex_);
            pressureFlag_.store(true);
        }
        pressureCv_.notify_one();
    }

private:
    ThreadPoolManager() = default;
    ~ThreadPoolManager() { shutdown(); }

    void rebalanceLoop() {
        using namespace std::chrono_literals;
        while (!stopFlag_.load()) {
            std::unique_lock lock(pressureMutex_);
            pressureCv_.wait(lock, [this] {
                return stopFlag_.load() || pressureFlag_.load();
            });
            if (stopFlag_.load()) return;
            pressureFlag_.store(false);

            const auto fuseQ  = fuse_->queueDepth();
            const auto syncQ  = sync_->queueDepth();
            const auto httpQ  = http_->queueDepth();
            const auto thumbQ = thumb_->queueDepth();
            const auto statsQ = stats_->queueDepth();

            // Compare backlog/worker ratios
            const auto fuseRatio  = fuseQ  / std::max(1u, fuse_->workerCount());
            const auto syncRatio  = syncQ  / std::max(1u, sync_->workerCount());
            const auto httpRatio  = httpQ  / std::max(1u, http_->workerCount());
            const auto thumbRatio = thumbQ / std::max(1u, thumb_->workerCount());
            const auto statsRatio = statsQ / std::max(1u, stats_->workerCount());

            maybeReassign(fuse_, http_, fuseRatio, httpRatio);
            maybeReassign(fuse_, stats_, fuseRatio, statsRatio);
            maybeReassign(sync_, thumb_, syncRatio, thumbRatio);
        }
    }

    static void maybeReassign(std::shared_ptr<ThreadPool>& hungry,
                   std::shared_ptr<ThreadPool>& donor,
                   unsigned int hungryRatio,
                   unsigned int donorRatio) {
        // If hungry backlog per worker > 4x donor backlog, steal
        if (hungryRatio > donorRatio * 4 && donor->hasIdleWorker()) {
            auto [t, flag] = donor->giveWorker();
            hungry->acceptWorker(std::move(t), flag);
        }
        // Return borrowed if hungry is calm
        else if (hungryRatio < 2 && hungry->hasBorrowedWorker()) {
            auto [t, flag] = hungry->giveWorker();
            donor->acceptWorker(std::move(t), flag);
        }
    }

    static constexpr unsigned int RESERVE_FACTOR = 3, NUM_POOLS = 4;
    std::shared_ptr<ThreadPool> fuse_, sync_, thumb_, http_, stats_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> running_{false};
    std::thread monitorThread_;
    unsigned int totalThreads_{0};
    std::condition_variable pressureCv_;
    std::mutex pressureMutex_;
    std::atomic<bool> pressureFlag_{false}; // true if pressure change signal received
};

} // namespace vh::concurrency
