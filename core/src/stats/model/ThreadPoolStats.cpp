#include "stats/model/ThreadPoolStats.hpp"

#include "concurrency/ThreadPoolManager.hpp"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <utility>

namespace vh::stats::model {

namespace {

std::uint64_t threadPoolUnixTimestamp() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(now));
}

std::string classifyPool(
    const bool present,
    const std::uint32_t workerCount,
    const std::uint64_t queueDepth,
    const std::uint32_t busyWorkerCount
) {
    if (!present || workerCount == 0) return "degraded";
    if (queueDepth == 0 && busyWorkerCount == 0) return "idle";
    if (queueDepth <= static_cast<std::uint64_t>(workerCount) * 2) return "normal";
    if (queueDepth <= static_cast<std::uint64_t>(workerCount) * 8) return "pressured";
    return "saturated";
}

ThreadPoolSnapshot convertPool(const concurrency::ThreadPoolManager::NamedThreadPoolSnapshot& raw) {
    const auto& source = raw.snapshot;
    const auto workerDivisor = std::max<std::uint32_t>(source.workerCount, 1);
    const auto status = classifyPool(
        raw.present,
        source.workerCount,
        source.queueDepth,
        source.busyWorkerCount
    );

    return {
        .name = raw.name,
        .queueDepth = static_cast<std::uint64_t>(source.queueDepth),
        .workerCount = source.workerCount,
        .borrowedWorkerCount = source.borrowedWorkerCount,
        .idleWorkerCount = source.idleWorkerCount,
        .busyWorkerCount = source.busyWorkerCount,
        .hasIdleWorker = source.hasIdleWorker,
        .hasBorrowedWorker = source.hasBorrowedWorker,
        .stopped = source.stopped,
        .pressureRatio = static_cast<double>(source.queueDepth) / static_cast<double>(workerDivisor),
        .status = status
    };
}

std::string threadPoolOverallStatus(
    const bool hasDegradedPool,
    const std::uint32_t saturatedPoolCount,
    const std::uint32_t pressuredPoolCount
) {
    if (hasDegradedPool) return "degraded";
    if (saturatedPoolCount > 0) return "saturated";
    if (pressuredPoolCount > 0) return "pressured";
    return "healthy";
}

}

ThreadPoolManagerSnapshot ThreadPoolManagerSnapshot::snapshot() {
    ThreadPoolManagerSnapshot out;
    const auto rawPools = concurrency::ThreadPoolManager::instance().snapshotPools();

    out.pools.reserve(rawPools.size());

    bool hasDegradedPool = false;
    for (const auto& raw : rawPools) {
        auto pool = convertPool(raw);

        out.totalWorkerCount += pool.workerCount;
        out.totalQueueDepth += pool.queueDepth;
        out.totalBorrowedWorkerCount += pool.borrowedWorkerCount;
        out.totalIdleWorkerCount += pool.idleWorkerCount;
        out.maxPressureRatio = std::max(out.maxPressureRatio, pool.pressureRatio);

        if (pool.status == "pressured") ++out.pressuredPoolCount;
        if (pool.status == "saturated") ++out.saturatedPoolCount;
        if (pool.status == "degraded") hasDegradedPool = true;

        out.pools.push_back(std::move(pool));
    }

    out.overallStatus = threadPoolOverallStatus(
        hasDegradedPool,
        out.saturatedPoolCount,
        out.pressuredPoolCount
    );
    out.checkedAt = threadPoolUnixTimestamp();

    return out;
}

void to_json(nlohmann::json& j, const ThreadPoolSnapshot& stats) {
    j = nlohmann::json{
        {"name", stats.name},
        {"queue_depth", stats.queueDepth},
        {"worker_count", stats.workerCount},
        {"borrowed_worker_count", stats.borrowedWorkerCount},
        {"idle_worker_count", stats.idleWorkerCount},
        {"busy_worker_count", stats.busyWorkerCount},
        {"has_idle_worker", stats.hasIdleWorker},
        {"has_borrowed_worker", stats.hasBorrowedWorker},
        {"stopped", stats.stopped},
        {"pressure_ratio", stats.pressureRatio},
        {"status", stats.status},
    };
}

void to_json(nlohmann::json& j, const ThreadPoolManagerSnapshot& stats) {
    j = nlohmann::json{
        {"overall_status", stats.overallStatus},
        {"total_worker_count", stats.totalWorkerCount},
        {"total_queue_depth", stats.totalQueueDepth},
        {"total_borrowed_worker_count", stats.totalBorrowedWorkerCount},
        {"total_idle_worker_count", stats.totalIdleWorkerCount},
        {"max_pressure_ratio", stats.maxPressureRatio},
        {"pressured_pool_count", stats.pressuredPoolCount},
        {"saturated_pool_count", stats.saturatedPoolCount},
        {"checked_at", stats.checkedAt},
        {"pools", stats.pools},
    };
}

}
