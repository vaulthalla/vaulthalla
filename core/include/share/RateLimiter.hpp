#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vh::share {

struct RateLimitPolicy {
    uint32_t max_attempts{};
    std::chrono::seconds window{0};
};

struct RateLimitDecision {
    bool allowed{};
    uint32_t remaining{};
    std::chrono::seconds retry_after{0};
};

class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    RateLimitDecision check(
        const std::string& key,
        const RateLimitPolicy& policy,
        Clock::time_point now = Clock::now()
    );

    void reset();
    void prune(Clock::time_point now = Clock::now());
    [[nodiscard]] std::size_t bucketCount() const;

private:
    struct Bucket {
        Clock::time_point window_start{};
        Clock::time_point last_seen{};
        uint32_t count{};
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}
