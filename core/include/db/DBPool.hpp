#pragma once

#include "DBConnection.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace vh::db {

class DBPool {
  public:
    explicit DBPool(const size_t size = 4) {
        for (size_t i = 0; i < size; ++i) pool_.push(std::make_unique<Connection>());
    }

    std::unique_ptr<Connection> acquire() {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [&]() { return !pool_.empty(); });
        auto conn = std::move(pool_.front());
        pool_.pop();
        return conn;
    }

    void release(std::unique_ptr<Connection> conn) {
        std::lock_guard lock(mtx_);
        pool_.push(std::move(conn));
        cv_.notify_one();
    }

    void initPreparedStatements() {
        for (size_t i = 0; i < pool_.size(); ++i) {
            auto conn = std::move(pool_.front());
            pool_.pop();
            conn->initPrepared();
            pool_.push(std::move(conn));
        }
    }

  private:
    std::queue<std::unique_ptr<Connection>> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

}
