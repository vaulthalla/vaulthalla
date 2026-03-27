#pragma once

#include "TestTask.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>

namespace vh::test::integration::concurrency {

class TestThreadPool {
public:
    explicit TestThreadPool(const std::shared_ptr<std::atomic<bool>>& interruptFlag,
               unsigned int nThreads = 0);

    ~TestThreadPool();

    void stop(std::chrono::milliseconds gracefulTimeout = std::chrono::milliseconds(1200));

    void submit(std::shared_ptr<TestTask> task);

    size_t queueDepth() const;

private:
    void spawnWorker();

    std::vector<std::thread> threads_;
    std::vector<std::shared_ptr<std::atomic<bool>>> idleFlags_;

    std::condition_variable cv;
    mutable std::mutex mutex;
    std::queue<std::shared_ptr<TestTask>> queue;

    std::shared_ptr<std::atomic<bool>> interruptFlag;
    std::atomic<bool> stopFlag{false};
};

} // namespace vh::concurrency
