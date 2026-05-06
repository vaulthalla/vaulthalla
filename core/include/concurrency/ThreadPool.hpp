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
    struct Snapshot {
        size_t queueDepth = 0;
        unsigned int workerCount = 0;
        unsigned int borrowedWorkerCount = 0;
        unsigned int idleWorkerCount = 0;
        unsigned int busyWorkerCount = 0;
        bool hasIdleWorker = false;
        bool hasBorrowedWorker = false;
        bool stopped = false;
    };

    using WorkerHandle = std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>;

    explicit ThreadPool(const std::shared_ptr<std::atomic<bool>>& interruptFlag,
               unsigned int nThreads = 0);

    ~ThreadPool();

    void stop(std::chrono::milliseconds gracefulTimeout = std::chrono::milliseconds(1200));

    void submit(std::shared_ptr<Task> task);

    size_t queueDepth() const;

    bool hasIdleWorker() const;
    bool isUnderloaded() const;
    bool hasBorrowedWorker() const;

    WorkerHandle giveWorker();
    WorkerHandle returnBorrowedWorker();

    void acceptWorker(std::thread t, std::shared_ptr<std::atomic<bool>> flag);
    void acceptReturnedWorker(std::thread t, std::shared_ptr<std::atomic<bool>> flag);

    [[nodiscard]] unsigned int workerCount() const;
    [[nodiscard]] Snapshot snapshot() const;

private:
    void spawnWorker();
    WorkerHandle takeWorker();
    void addWorker(std::thread t, std::shared_ptr<std::atomic<bool>> flag);
    void decrementBorrowedWorkerCount();

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
