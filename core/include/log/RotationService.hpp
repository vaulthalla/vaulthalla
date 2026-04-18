#pragma once

#include "concurrency/AsyncService.hpp"

#include <memory>

namespace vh::log {

class Rotator;

class RotationService final : public concurrency::AsyncService {
public:
    RotationService();
    ~RotationService() override;

    void runLoop() override;

private:
    std::unique_ptr<Rotator> appRot_, auditRot_;
};

}
