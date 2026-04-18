#pragma once

#include "concurrency/AsyncService.hpp"

#include <chrono>

namespace vh::db {

class Janitor final : public concurrency::AsyncService {
public:
    Janitor();
    ~Janitor() override = default;

protected:
    void runLoop() override;

private:
    std::chrono::minutes sweep_interval_;
};

}
