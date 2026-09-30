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

    // auth.login counts failed attempts only: check() gates it without recording, and the login handler
    // calls this when authentication fails. Counting successes locked out legitimate repeated logins
    // (scripts, several browser tabs) that all appear as 127.0.0.1 behind the nginx proxy.
    void recordLoginFailure(std::string_view accountName, const Session& session, Clock::time_point now = Clock::now());

    void reset();
    [[nodiscard]] std::size_t bucketCount() const;

    [[nodiscard]] static bool isLimitedCommand(std::string_view command);

    // Process-wide limiter shared by the ws Router and the login handler.
    [[nodiscard]] static ShareRateLimit& instance();

private:
    vh::share::RateLimiter limiter_;
};

}
