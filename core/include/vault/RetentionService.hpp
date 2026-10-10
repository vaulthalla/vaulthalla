#pragma once

#include "concurrency/AsyncService.hpp"

#include <chrono>

namespace vh::vault {

// Runs vault::retention passes (#162): purges deleted vaults whose retention window ended (or that were deleted
// "now"), resumes purges a stop interrupted, and drops key copies whose key retention ended. Every kInterval, or
// sooner when a "delete now" asks for a pass. Its own thread: never a FUSE thread, never inside a DB transaction.
class RetentionService final : public concurrency::AsyncService {
public:
    static constexpr auto kInterval = std::chrono::seconds(30);

    RetentionService();
    ~RetentionService() override = default;

protected:
    void runLoop() override;
};

}
