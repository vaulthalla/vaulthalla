#pragma once

#include "concurrency/ThreadPool.hpp"
#include "log/Registry.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vh::concurrency {

class ThreadPoolManager {
public:
    struct NamedThreadPoolSnapshot {
        std::string name;
        bool present = false;
        ThreadPool::Snapshot snapshot;
    };

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
        unsigned int thumbN = base + (rem > 2 ? 1 : 0);
        unsigned int httpN  = base + (rem > 3 ? 1 : 0);
        unsigned int statsN = base + (rem > 4 ? 1 : 0);
        unsigned int s3N    = base + (rem > 5 ? 1 : 0);

        fuse_  = std::make_shared<ThreadPool>(fuseN);
        sync_  = std::make_shared<ThreadPool>(syncN);
        thumb_ = std::make_shared<ThreadPool>(thumbN);
        http_  = std::make_shared<ThreadPool>(httpN);
        stats_ = std::make_shared<ThreadPool>(statsN);
        s3_    = std::make_shared<ThreadPool>(s3N);
    }

    // Every pool is asked to stop before any is joined, and all joins share one deadline, so shutdown waits for
    // the slowest in-flight task (bounded by `timeout`), never for the sum of the pools.
    void shutdown(const std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        if (!running_.exchange(false)) return;

        const auto pools = all();
        for (const auto& pool : pools) pool->requestStop();

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        unsigned int detached = 0;
        for (const auto& pool : pools) detached += pool->join(deadline);

        if (detached)
            log::Registry::runtime()->warn("[ThreadPoolManager] {} worker(s) still running a task after {} ms; detached",
                                           detached, timeout.count());
        else log::Registry::runtime()->info("[ThreadPoolManager] Thread pools stopped.");
    }

    std::shared_ptr<ThreadPool>& fusePool() { return fuse_; }
    std::shared_ptr<ThreadPool>& syncPool() { return sync_; }
    std::shared_ptr<ThreadPool>& thumbPool() { return thumb_; }
    std::shared_ptr<ThreadPool>& httpPool() { return http_; }
    std::shared_ptr<ThreadPool>& statsPool() { return stats_; }
    std::shared_ptr<ThreadPool>& s3Pool() { return s3_; }

    [[nodiscard]] std::vector<NamedThreadPoolSnapshot> snapshotPools() const {
        return {
            namedSnapshot("fuse", fuse_),
            namedSnapshot("sync", sync_),
            namedSnapshot("thumb", thumb_),
            namedSnapshot("http", http_),
            namedSnapshot("stats", stats_),
            namedSnapshot("s3", s3_)
        };
    }

private:
    ThreadPoolManager() = default;
    ~ThreadPoolManager() { shutdown(); }

    static NamedThreadPoolSnapshot namedSnapshot(
        std::string name,
        const std::shared_ptr<ThreadPool>& pool
    ) {
        if (!pool) return {
            .name = std::move(name),
            .present = false,
            .snapshot = {}
        };
        return {
            .name = std::move(name),
            .present = true,
            .snapshot = pool->snapshot()
        };
    }

    static constexpr unsigned int RESERVE_FACTOR = 3, NUM_POOLS = 6;
    std::shared_ptr<ThreadPool> fuse_, sync_, thumb_, http_, stats_, s3_;
    std::atomic<bool> running_{false};
    unsigned int totalThreads_{0};

    [[nodiscard]] std::vector<std::shared_ptr<ThreadPool>> all() const {
        std::vector<std::shared_ptr<ThreadPool>> pools;
        for (const auto& pool : {fuse_, sync_, thumb_, http_, stats_, s3_})
            if (pool) pools.push_back(pool);
        return pools;
    }
};

} // namespace vh::concurrency
