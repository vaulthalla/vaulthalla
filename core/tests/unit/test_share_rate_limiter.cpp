#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/ShareRateLimit.hpp"
#include "share/RateLimiter.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace {
using vh::protocols::ws::Router;
using vh::protocols::ws::Session;
using vh::protocols::ws::ShareRateLimit;
using vh::share::RateLimitPolicy;
using vh::share::RateLimiter;

constexpr std::string_view kPublicToken = "vhs_00000000-0000-4000-8000-000000000001_secretA";
constexpr std::string_view kSameLookupDifferentSecret = "vhs_00000000-0000-4000-8000-000000000001_secretB";

std::shared_ptr<Session> publicSession() {
    auto session = std::make_shared<Session>(std::make_shared<Router>());
    session->ipAddress = "203.0.113.10";
    session->userAgent = "rate-limit-test";
    return session;
}
}

TEST(ShareRateLimiterTest, FixedWindowDeniesAfterBurstAndResetsAfterWindow) {
    RateLimiter limiter;
    const RateLimitPolicy policy{.max_attempts = 2, .window = std::chrono::seconds{10}};
    const auto start = RateLimiter::Clock::time_point{} + std::chrono::seconds{100};

    EXPECT_TRUE(limiter.check("key", policy, start).allowed);
    EXPECT_TRUE(limiter.check("key", policy, start + std::chrono::seconds{1}).allowed);
    const auto denied = limiter.check("key", policy, start + std::chrono::seconds{2});
    EXPECT_FALSE(denied.allowed);
    EXPECT_EQ(0u, denied.remaining);
    EXPECT_GT(denied.retry_after.count(), 0);

    EXPECT_TRUE(limiter.check("key", policy, start + std::chrono::seconds{10}).allowed);
}

TEST(ShareRateLimiterTest, PrunesIdleBuckets) {
    RateLimiter limiter;
    const RateLimitPolicy policy{.max_attempts = 2, .window = std::chrono::seconds{10}};
    const auto start = RateLimiter::Clock::time_point{} + std::chrono::seconds{100};

    EXPECT_TRUE(limiter.check("one", policy, start).allowed);
    EXPECT_TRUE(limiter.check("two", policy, start).allowed);
    EXPECT_EQ(2u, limiter.bucketCount());

    limiter.prune(start + std::chrono::minutes{31});
    EXPECT_EQ(0u, limiter.bucketCount());
}

TEST(ShareRateLimiterTest, PublicSessionOpenUsesLookupIdNotRawSecret) {
    ShareRateLimit limiter;
    const auto session = publicSession();
    const auto now = ShareRateLimit::Clock::time_point{} + std::chrono::seconds{1000};

    nlohmann::json first = {{"payload", {{"public_token", std::string(kPublicToken)}}}};
    nlohmann::json sameLookup = {{"payload", {{"public_token", std::string(kSameLookupDifferentSecret)}}}};

    for (int i = 0; i < 12; ++i)
        EXPECT_TRUE(limiter.check("share.session.open", first, *session, now + std::chrono::seconds{i}).allowed);

    EXPECT_FALSE(limiter.check("share.session.open", sameLookup, *session, now + std::chrono::seconds{12}).allowed);
}

TEST(ShareRateLimiterTest, EmailConfirmLimitsBySessionAndChallenge) {
    ShareRateLimit limiter;
    const auto session = publicSession();
    session->setPendingShareSession("session-1", "vhss_pending");
    const auto now = ShareRateLimit::Clock::time_point{} + std::chrono::seconds{2000};
    nlohmann::json msg = {{"payload", {{"challenge_id", "challenge-1"}, {"code", "123456"}}}};

    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(limiter.check("share.email.challenge.confirm", msg, *session, now + std::chrono::seconds{i}).allowed);

    EXPECT_FALSE(limiter.check("share.email.challenge.confirm", msg, *session, now + std::chrono::seconds{8}).allowed);
}

TEST(ShareRateLimiterTest, CoversPublicAndTransferCommandsButNotCleanupCommands) {
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.session.open"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.email.challenge.start"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.email.challenge.confirm"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.fs.metadata"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.fs.list"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.preview.get"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.download.start"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.download.chunk"));
    EXPECT_TRUE(ShareRateLimit::isLimitedCommand("share.upload.start"));

    EXPECT_FALSE(ShareRateLimit::isLimitedCommand("share.download.cancel"));
    EXPECT_FALSE(ShareRateLimit::isLimitedCommand("share.upload.finish"));
    EXPECT_FALSE(ShareRateLimit::isLimitedCommand("share.upload.cancel"));
    EXPECT_FALSE(ShareRateLimit::isLimitedCommand("share.link.create"));
}
