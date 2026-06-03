#pragma once

#include "concurrency/Task.hpp"
#include "protocols/s3/Session.hpp"

#include <memory>
#include <utility>

namespace vh::protocols::s3::task {

struct AsyncSession final : concurrency::Task {
    std::shared_ptr<Session> session;

    explicit AsyncSession(std::shared_ptr<Session> s) : session(std::move(s)) {}

    void operator()() override {
        session->run();
    }
};

} // namespace vh::protocols::s3::task
