#pragma once

#include "concurrency/Task.hpp"
#include "protocols/http/Session.hpp"

namespace vh::protocols::http::task {

struct AsyncSession final : concurrency::Task {
    std::shared_ptr<Session> session;

    explicit AsyncSession(std::shared_ptr<Session> s) : session(std::move(s)) {}

    void operator()() override {
        session->run();
    }
};


}
