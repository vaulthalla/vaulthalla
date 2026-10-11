#pragma once

#define FUSE_USE_VERSION 35

#include "concurrency/AsyncService.hpp"
#include "storage/Fwd.hpp"

#include <fuse_lowlevel.h>

#include <atomic>

namespace vh::fuse {

class Service final : public concurrency::AsyncService {
    friend class ServiceManager;

public:

    explicit Service();

    [[nodiscard]] fuse_session* session() const noexcept { return session_; }

protected:
    void runLoop() override;
    // Lazily unmounts, ends the session and, if the receive loop is still blocked after a grace period (something
    // holds the mount open), aborts the kernel connection so the loop and the stop can never hang.
    void onStop() override;

private:
    fuse_session* session_{nullptr};
    std::atomic<bool> loopActive_{false};
};

}
