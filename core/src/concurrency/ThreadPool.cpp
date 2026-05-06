#include "concurrency/ThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"

#include <algorithm>
#include <ranges>

using namespace vh::concurrency;

ThreadPool::ThreadPool(const std::shared_ptr<std::atomic<bool> > &interruptFlag,
                       unsigned int nThreads)
    : interruptFlag(interruptFlag), stopFlag(false) {
    for (unsigned int i = 0; i < nThreads; ++i) {
        spawnWorker();
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop(std::chrono::milliseconds gracefulTimeout) { {
        std::scoped_lock lock(mutex);
        std::queue<std::shared_ptr<Task> > empty;
        std::swap(queue, empty);
    }

    stopFlag.store(true);
    cv.notify_all();

    std::vector<std::thread> threads;
    {
        std::scoped_lock lock(mutex);
        threads.swap(threads_);
        idleFlags_.clear();
    }

    for (auto &t: threads) {
        if (!t.joinable()) continue;

        if (t.joinable()) {
            if (t.joinable()) {
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - start < gracefulTimeout) {
                    if (t.joinable()) {
                        try {
                            t.join();
                            break;
                        } catch (...) {
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (t.joinable()) t.detach(); // force release
            }
        }
    }
}

void ThreadPool::submit(std::shared_ptr<Task> task) { {
        std::scoped_lock lock(mutex);
        queue.push(std::move(task));
    }
    cv.notify_one();

    // Notify manager about load change
    ThreadPoolManager::instance().signalPressureChange();
}

size_t ThreadPool::queueDepth() const {
    std::scoped_lock lock(mutex);
    return queue.size();
}

bool ThreadPool::hasIdleWorker() const {
    std::scoped_lock lock(mutex);
    return std::ranges::any_of(idleFlags_, [](auto &flag) { return flag->load(); });
}

bool ThreadPool::isUnderloaded() const {
    return queueDepth() == 0;
}

bool ThreadPool::hasBorrowedWorker() const {
    return borrowedCount_.load() > 0;
}

ThreadPool::WorkerHandle ThreadPool::takeWorker() {
    std::scoped_lock lock(mutex);
    if (threads_.empty()) throw std::runtime_error("No workers to give");
    auto t = std::move(threads_.back());
    threads_.pop_back();

    auto f = idleFlags_.back();
    idleFlags_.pop_back();

    return {std::move(t), f};
}

void ThreadPool::addWorker(std::thread t, std::shared_ptr<std::atomic<bool> > flag) {
    std::scoped_lock lock(mutex);
    threads_.push_back(std::move(t));
    idleFlags_.push_back(std::move(flag));
}

void ThreadPool::decrementBorrowedWorkerCount() {
    auto current = borrowedCount_.load();
    while (current > 0 && !borrowedCount_.compare_exchange_weak(current, current - 1)) {}
}

ThreadPool::WorkerHandle ThreadPool::giveWorker() {
    return takeWorker();
}

ThreadPool::WorkerHandle ThreadPool::returnBorrowedWorker() {
    auto worker = takeWorker();
    decrementBorrowedWorkerCount();
    return worker;
}

void ThreadPool::acceptWorker(std::thread t, std::shared_ptr<std::atomic<bool> > flag) {
    addWorker(std::move(t), std::move(flag));
    borrowedCount_.fetch_add(1);
}

void ThreadPool::acceptReturnedWorker(std::thread t, std::shared_ptr<std::atomic<bool> > flag) {
    addWorker(std::move(t), std::move(flag));
}

unsigned int ThreadPool::workerCount() const {
    std::scoped_lock lock(mutex);
    return static_cast<unsigned int>(threads_.size());
}

ThreadPool::Snapshot ThreadPool::snapshot() const {
    std::scoped_lock lock(mutex);

    const auto workerCount = static_cast<unsigned int>(threads_.size());
    const auto idleWorkerCount = static_cast<unsigned int>(std::ranges::count_if(
        idleFlags_,
        [](const auto& flag) { return flag && flag->load(); }
    ));

    return {
        .queueDepth = queue.size(),
        .workerCount = workerCount,
        .borrowedWorkerCount = borrowedCount_.load(),
        .idleWorkerCount = idleWorkerCount,
        .busyWorkerCount = workerCount > idleWorkerCount ? workerCount - idleWorkerCount : 0,
        .hasIdleWorker = idleWorkerCount > 0,
        .hasBorrowedWorker = borrowedCount_.load() > 0,
        .stopped = stopFlag.load()
    };
}

void ThreadPool::spawnWorker() {
    auto flag = std::make_shared<std::atomic<bool> >(true); // idle at start
    idleFlags_.push_back(flag);

    threads_.emplace_back([this, flag] {
        while (true) {
            std::shared_ptr<Task> task; {
                std::unique_lock lock(mutex);
                cv.wait(lock, [this] {
                    return stopFlag.load() || !queue.empty();
                });

                if (stopFlag.load() && queue.empty()) break;

                task = std::move(queue.front());
                queue.pop();
            }

            if (task) {
                flag->store(false);
                try {
                    (*task)();
                } catch (...) {
                    // log and swallow so shutdown isn't blocked
                }
                flag->store(true);
            }
        }
    });
}
