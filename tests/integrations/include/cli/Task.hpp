#pragma once

#include "TestCase.hpp"
#include "concurrency/TestTask.hpp"
#include "cmd/Router.hpp"

namespace vh::test::integration::cli {

class Task final : public concurrency::PromisedTestTask {
public:
    explicit Task(const std::shared_ptr<cmd::Router>& router,
                         const std::vector<std::shared_ptr<TestCase>>& tests)
        : PromisedTestTask(), router_(router), tests_(tests) {
        if (!router_) throw std::runtime_error("CLITestTask: router is null");
    }
    ~Task() override = default;

    void operator()() override {
        try { promise.set_value(router_->route(tests_)); }
        catch (const std::exception& e) { promise.set_exception(std::current_exception()); }
    }

private:
    std::shared_ptr<cmd::Router> router_;
    std::vector<std::shared_ptr<TestCase>> tests_;
};

}
