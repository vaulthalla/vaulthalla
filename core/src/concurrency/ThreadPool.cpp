#include "concurrency/ThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"

#include <ranges>
#include <algorithm>

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

    for (auto &t: threads_) {
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

    threads_.clear();
    idleFlags_.clear();
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
    return std::ranges::any_of(idleFlags_, [](auto &flag) { return flag->load(); });
}

bool ThreadPool::isUnderloaded() const {
    return queueDepth() == 0;
}

bool ThreadPool::hasBorrowedWorker() const {
    return borrowedCount_ > 0;
}

std::pair<std::thread, std::shared_ptr<std::atomic<bool> > >
ThreadPool::giveWorker() {
    if (threads_.empty()) throw std::runtime_error("No workers to give");
    auto t = std::move(threads_.back());
    threads_.pop_back();

    auto f = idleFlags_.back();
    idleFlags_.pop_back();

    borrowedCount_--;
    return {std::move(t), f};
}

void ThreadPool::acceptWorker(std::thread t, std::shared_ptr<std::atomic<bool> > flag) {
    threads_.push_back(std::move(t));
    idleFlags_.push_back(std::move(flag));
    borrowedCount_++;
}

unsigned int ThreadPool::workerCount() const {
    return threads_.size();
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