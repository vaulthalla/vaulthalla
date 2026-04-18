#pragma once

#include "Task.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>

namespace vh::concurrency {

class ThreadPoolManager; // forward decl

class ThreadPool {
public:
    explicit ThreadPool(const std::shared_ptr<std::atomic<bool>>& interruptFlag,
               unsigned int nThreads = 0);

    ~ThreadPool();

    void stop(std::chrono::milliseconds gracefulTimeout = std::chrono::milliseconds(1200));

    void submit(std::shared_ptr<Task> task);

    size_t queueDepth() const;

    bool hasIdleWorker() const;
    bool isUnderloaded() const;
    bool hasBorrowedWorker() const;

    std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>
    giveWorker();

    void acceptWorker(std::thread t, std::shared_ptr<std::atomic<bool>> flag);

    [[nodiscard]] unsigned int workerCount() const;

private:
    void spawnWorker();

    std::vector<std::thread> threads_;
    std::vector<std::shared_ptr<std::atomic<bool>>> idleFlags_;
    std::atomic<unsigned int> borrowedCount_{0};

    std::condition_variable cv;
    mutable std::mutex mutex;
    std::queue<std::shared_ptr<Task>> queue;

    std::shared_ptr<std::atomic<bool>> interruptFlag;
    std::atomic<bool> stopFlag{false};
};

} // namespace vh::concurrency
