#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct ThreadPoolSnapshot {
    std::string name;
    std::uint64_t queueDepth = 0;
    std::uint32_t workerCount = 0;
    std::uint32_t borrowedWorkerCount = 0;
    std::uint32_t idleWorkerCount = 0;
    std::uint32_t busyWorkerCount = 0;
    bool hasIdleWorker = false;
    bool hasBorrowedWorker = false;
    bool stopped = false;
    double pressureRatio = 0.0;
    std::string status = "degraded";
};

struct ThreadPoolManagerSnapshot {
    std::vector<ThreadPoolSnapshot> pools;
    std::uint32_t totalWorkerCount = 0;
    std::uint64_t totalQueueDepth = 0;
    std::uint32_t totalBorrowedWorkerCount = 0;
    std::uint32_t totalIdleWorkerCount = 0;
    double maxPressureRatio = 0.0;
    std::uint32_t saturatedPoolCount = 0;
    std::uint32_t pressuredPoolCount = 0;
    std::string overallStatus = "degraded";
    std::uint64_t checkedAt = 0;

    static ThreadPoolManagerSnapshot snapshot();
};

void to_json(nlohmann::json& j, const ThreadPoolSnapshot& stats);
void to_json(nlohmann::json& j, const ThreadPoolManagerSnapshot& stats);

}
