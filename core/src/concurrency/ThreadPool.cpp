#include "concurrency/ThreadPool.hpp"

namespace vh::concurrency {

ThreadPool::ThreadPool(const unsigned int nThreads) {
    workers_.reserve(nThreads);
    for (unsigned int i = 0; i < nThreads; ++i) {
        {
            std::scoped_lock lock(state_->mutex);
            ++state_->live;
        }
        auto exited = std::make_shared<std::atomic<bool>>(false);
        workers_.push_back({std::thread([state = state_, exited] { workerLoop(state, exited); }), exited});
    }
}

ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::workerLoop(const std::shared_ptr<State>& state, const std::shared_ptr<std::atomic<bool>>& exited) {
    std::unique_lock lock(state->mutex);
    while (true) {
        state->work.wait(lock, [&] { return state->stopping || !state->queue.empty(); });
        if (state->stopping) break;

        auto task = std::move(state->queue.front());
        state->queue.pop();
        ++state->busy;
        lock.unlock();
        try {
            if (task) (*task)();
        } catch (...) {
            // A task's failure is its own; the worker keeps serving the queue.
        }
        task.reset();  // release the task (and any promise it holds) outside the lock
        lock.lock();
        --state->busy;
        if (state->busy == 0 && state->queue.empty()) state->exited.notify_all();  // waitIdle()
    }
    --state->live;
    exited->store(true);
    state->exited.notify_all();
}

void ThreadPool::submit(std::shared_ptr<Task> task) {
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->stopping) return;  // shutting down: the task (and its promise) is dropped
        state_->queue.push(std::move(task));
    }
    state_->work.notify_one();
}

void ThreadPool::requestStop() {
    std::queue<std::shared_ptr<Task>> dropped;
    {
        std::scoped_lock lock(state_->mutex);
        state_->stopping = true;
        std::swap(dropped, state_->queue);
    }
    state_->work.notify_all();
    // `dropped` destroys the queued tasks here, outside the lock: waiters on their futures see broken_promise.
}

unsigned int ThreadPool::join(const std::chrono::steady_clock::time_point deadline) {
    if (workers_.empty()) return 0;
    {
        std::unique_lock lock(state_->mutex);
        state_->exited.wait_until(lock, deadline, [&] { return state_->live == 0; });
    }

    // Workers that left their loop are joined; the rest are still running a task past the deadline and are detached
    // (they hold the shared state, never this object).
    unsigned int detached = 0;
    for (auto& worker : workers_) {
        if (!worker.thread.joinable()) continue;
        if (worker.exited->load()) worker.thread.join();
        else {
            worker.thread.detach();
            ++detached;
        }
    }
    workers_.clear();
    return detached;
}

unsigned int ThreadPool::stop(const std::chrono::milliseconds timeout) {
    requestStop();
    return join(std::chrono::steady_clock::now() + timeout);
}

bool ThreadPool::waitIdle(const std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(state_->mutex);
    return state_->exited.wait_until(lock, deadline, [&] { return state_->busy == 0 && state_->queue.empty(); });
}

size_t ThreadPool::queueDepth() const {
    std::scoped_lock lock(state_->mutex);
    return state_->queue.size();
}

unsigned int ThreadPool::workerCount() const {
    std::scoped_lock lock(state_->mutex);
    return state_->live;
}

ThreadPool::Snapshot ThreadPool::snapshot() const {
    std::scoped_lock lock(state_->mutex);
    const auto workers = state_->live;
    const auto busy = std::min(state_->busy, workers);
    return {
        .queueDepth = state_->queue.size(),
        .workerCount = workers,
        .idleWorkerCount = workers - busy,
        .busyWorkerCount = busy,
        .hasIdleWorker = workers > busy,
        .stopped = state_->stopping,
    };
}

}
