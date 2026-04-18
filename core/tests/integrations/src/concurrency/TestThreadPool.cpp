#include "concurrency/TestThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"

#include <ranges>
#include <algorithm>

namespace vh::test::integration::concurrency {
    TestThreadPool::TestThreadPool(const std::shared_ptr<std::atomic<bool> >& interruptFlag,
                           const unsigned int nThreads)
        : interruptFlag(interruptFlag), stopFlag(false) {
        for (unsigned int i = 0; i < nThreads; ++i) {
            spawnWorker();
        }
    }

    TestThreadPool::~TestThreadPool() {
        stop();
    }

    void TestThreadPool::stop(std::chrono::milliseconds gracefulTimeout) { {
        std::scoped_lock lock(mutex);
        std::queue<std::shared_ptr<TestTask> > empty;
        std::swap(queue, empty);
    }

        stopFlag.store(true);
        cv.notify_all();

        for (auto& t : threads_) {
            if (!t.joinable()) continue;

            if (t.joinable()) {
                if (t.joinable()) {
                    auto start = std::chrono::steady_clock::now();
                    while (std::chrono::steady_clock::now() - start < gracefulTimeout) {
                        if (t.joinable()) {
                            try { t.join(); break; } catch (...) {}
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

    void TestThreadPool::submit(std::shared_ptr<TestTask> task) { {
        std::scoped_lock lock(mutex);
        queue.push(std::move(task));
    }
        cv.notify_one();
    }

    size_t TestThreadPool::queueDepth() const {
        std::scoped_lock lock(mutex);
        return queue.size();
    }

    void TestThreadPool::spawnWorker() {
        auto flag = std::make_shared<std::atomic<bool>>(true); // idle at start
        idleFlags_.push_back(flag);

        threads_.emplace_back([this, flag] {
            while (true) {
                std::shared_ptr<TestTask> task; {
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
}
