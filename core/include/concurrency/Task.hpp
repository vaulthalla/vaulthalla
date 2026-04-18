#pragma once

#include "types.hpp"

#include <future>
#include <optional>

namespace vh::concurrency {

struct Task {
    virtual ~Task() = default;
    virtual void operator()() = 0;

    // Optional future for reporting
    virtual std::optional<std::future<ExpectedFuture>> getFuture() { return std::nullopt; }
};

struct PromisedTask : Task {
    std::promise<ExpectedFuture> promise;

    PromisedTask() = default;
    explicit PromisedTask(std::promise<ExpectedFuture> p) : promise(std::move(p)) {}

    std::optional<std::future<ExpectedFuture>> getFuture() override { return promise.get_future(); }

    void operator()() override { throw std::runtime_error("PromisedTask must implement operator()()"); }
};

}
