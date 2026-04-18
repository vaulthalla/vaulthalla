#pragma once

#include "TestCase.hpp"

#include <future>
#include <optional>
#include <variant>

namespace vh::test::integration::concurrency {

typedef std::variant<std::vector<std::shared_ptr<TestCase>>> TestFuture;

struct TestTask {
    virtual ~TestTask() = default;
    virtual void operator()() = 0;

    // Optional future for reporting
    virtual std::optional<std::future<TestFuture>> getFuture() { return std::nullopt; }
};

struct PromisedTestTask : TestTask {
    std::promise<TestFuture> promise;

    PromisedTestTask() = default;
    explicit PromisedTestTask(std::promise<TestFuture> p) : promise(std::move(p)) {}

    std::optional<std::future<TestFuture>> getFuture() override { return promise.get_future(); }

    void operator()() override { throw std::runtime_error("PromisedTask must implement operator()()"); }
};

}
