#pragma once

#include "concurrency/AsyncService.hpp"

#include <chrono>

namespace vh::protocols::ws {

class ConnectionLifecycleManager final : public concurrency::AsyncService {
  public:
    ConnectionLifecycleManager();
    ~ConnectionLifecycleManager() override;

  private:
    void runLoop() override;

    void sweepActiveSessions() const;

    std::chrono::seconds sweep_interval_{30};
    std::chrono::seconds unauthenticated_session_timeout_{60};
    std::chrono::minutes idle_timeout_{30};
};

}
