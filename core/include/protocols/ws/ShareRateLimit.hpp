#pragma once

#include "share/RateLimiter.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace vh::protocols::ws {

class Session;

class ShareRateLimit {
public:
    using Clock = vh::share::RateLimiter::Clock;
    using json = nlohmann::json;

    [[nodiscard]] vh::share::RateLimitDecision check(
        std::string_view command,
        const json& message,
        const Session& session,
        Clock::time_point now = Clock::now()
    );

    void reset();
    [[nodiscard]] std::size_t bucketCount() const;

    [[nodiscard]] static bool isLimitedCommand(std::string_view command);

private:
    vh::share::RateLimiter limiter_;
};

}
