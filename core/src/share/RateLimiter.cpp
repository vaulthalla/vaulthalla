#include "share/RateLimiter.hpp"

#include <algorithm>
#include <stdexcept>

namespace vh::share {
namespace {
constexpr std::size_t kMaxBuckets = 4096;
constexpr auto kIdleBucketTtl = std::chrono::minutes(30);
}

RateLimitDecision RateLimiter::check(
    const std::string& key,
    const RateLimitPolicy& policy,
    const Clock::time_point now
) {
    if (key.empty()) throw std::invalid_argument("Rate limit key is required");
    if (policy.max_attempts == 0) throw std::invalid_argument("Rate limit max attempts is required");
    if (policy.window <= std::chrono::seconds{0}) throw std::invalid_argument("Rate limit window is required");

    std::scoped_lock lock(mutex_);
    if (buckets_.size() >= kMaxBuckets) {
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            if (now - it->second.last_seen >= kIdleBucketTtl) it = buckets_.erase(it);
            else ++it;
        }
        if (buckets_.size() >= kMaxBuckets) {
            auto oldest = std::ranges::min_element(buckets_, {}, [](const auto& item) {
                return item.second.last_seen;
            });
            if (oldest != buckets_.end()) buckets_.erase(oldest);
        }
    }

    auto& bucket = buckets_[key];
    if (bucket.count == 0 || now - bucket.window_start >= policy.window) {
        bucket.window_start = now;
        bucket.count = 0;
    }
    bucket.last_seen = now;

    if (bucket.count >= policy.max_attempts) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - bucket.window_start);
        const auto retry = elapsed >= policy.window ? std::chrono::seconds{0} : policy.window - elapsed;
        return {
            .allowed = false,
            .remaining = 0,
            .retry_after = retry
        };
    }

    ++bucket.count;
    return {
        .allowed = true,
        .remaining = policy.max_attempts - bucket.count,
        .retry_after = std::chrono::seconds{0}
    };
}

void RateLimiter::reset() {
    std::scoped_lock lock(mutex_);
    buckets_.clear();
}

void RateLimiter::prune(const Clock::time_point now) {
    std::scoped_lock lock(mutex_);
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        if (now - it->second.last_seen >= kIdleBucketTtl) it = buckets_.erase(it);
        else ++it;
    }
}

std::size_t RateLimiter::bucketCount() const {
    std::scoped_lock lock(mutex_);
    return buckets_.size();
}

}
