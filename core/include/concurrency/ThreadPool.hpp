#pragma once

#include "Task.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vh::concurrency {

// A fixed set of workers draining one task queue. Shutdown is two-phase so a daemon stops in bounded time:
// requestStop() drops queued tasks and wakes every worker without blocking, join() then waits until a deadline
// for workers to finish the task they are running. Workers share their state through a shared_ptr, so a worker
// that misses the deadline can be detached safely (it never touches the ThreadPool object again).
class ThreadPool {
public:
    struct Snapshot {
        size_t queueDepth = 0;
        unsigned int workerCount = 0;
        unsigned int idleWorkerCount = 0;
        unsigned int busyWorkerCount = 0;
        bool hasIdleWorker = false;
        bool stopped = false;
    };

    explicit ThreadPool(unsigned int nThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::shared_ptr<Task> task);

    // Phase one: refuse new work, drop queued tasks and wake every idle worker. Never blocks.
    void requestStop();

    // Phase two: waits until `deadline` for workers to finish their current task and joins them; workers still
    // busy at the deadline are detached. Returns how many were detached.
    unsigned int join(std::chrono::steady_clock::time_point deadline);

    // requestStop() + join() with a deadline `timeout` from now.
    unsigned int stop(std::chrono::milliseconds timeout = std::chrono::seconds(2));

    // Waits until `deadline` for the queue to drain and every running task to finish (the pool keeps running).
    // Returns true if it went idle.
    bool waitIdle(std::chrono::steady_clock::time_point deadline);

    [[nodiscard]] size_t queueDepth() const;
    [[nodiscard]] unsigned int workerCount() const;
    [[nodiscard]] Snapshot snapshot() const;

private:
    struct State {
        mutable std::mutex mutex;
        std::condition_variable work;    // tasks queued or stop requested
        std::condition_variable exited;  // a worker left its loop, or the pool went idle
        std::queue<std::shared_ptr<Task>> queue;
        bool stopping = false;
        unsigned int busy = 0;
        unsigned int live = 0;           // workers still inside their loop
    };

    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> exited;  // set when the worker leaves its loop
    };

    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::vector<Worker> workers_;

    static void workerLoop(const std::shared_ptr<State>& state, const std::shared_ptr<std::atomic<bool>>& exited);
};

} // namespace vh::concurrency
