#include "protocols/ws/ShareRateLimit.hpp"

#include "protocols/ws/Session.hpp"
#include "share/Principal.hpp"
#include "share/Token.hpp"

#include <format>
#include <optional>
#include <string>

namespace vh::protocols::ws {
namespace {
using vh::share::RateLimitPolicy;
using vh::share::TokenKind;

[[nodiscard]] std::optional<RateLimitPolicy> policyFor(const std::string_view command) {
    using namespace std::chrono_literals;

    if (command == "share.session.open")
        return RateLimitPolicy{.max_attempts = 12, .window = 5min};
    if (command == "share.email.challenge.start")
        return RateLimitPolicy{.max_attempts = 5, .window = 15min};
    if (command == "share.email.challenge.confirm")
        return RateLimitPolicy{.max_attempts = 8, .window = 15min};
    if (command == "share.fs.metadata" || command == "share.fs.list")
        return RateLimitPolicy{.max_attempts = 120, .window = 1min};
    if (command == "share.preview.get")
        return RateLimitPolicy{.max_attempts = 60, .window = 1min};
    if (command == "share.download.start")
        return RateLimitPolicy{.max_attempts = 30, .window = 1min};
    if (command == "share.download.chunk")
        return RateLimitPolicy{.max_attempts = 1200, .window = 1min};
    if (command == "share.upload.start")
        return RateLimitPolicy{.max_attempts = 20, .window = 1min};

    return std::nullopt;
}

[[nodiscard]] const nlohmann::json& payloadOf(const nlohmann::json& message) {
    static const nlohmann::json empty = nlohmann::json::object();
    if (!message.is_object() || !message.contains("payload") || !message.at("payload").is_object()) return empty;
    return message.at("payload");
}

[[nodiscard]] std::string optionalString(const nlohmann::json& payload, const char* field) {
    if (!payload.contains(field) || payload.at(field).is_null()) return "";
    if (!payload.at(field).is_string()) return "";
    return payload.at(field).get<std::string>();
}

[[nodiscard]] std::string tokenLookupKey(
    const nlohmann::json& payload,
    const char* field,
    const TokenKind expectedKind
) {
    const auto raw = optionalString(payload, field);
    if (raw.empty()) return "token:none";

    try {
        const auto parsed = vh::share::Token::parse(raw);
        if (parsed.kind != expectedKind) return "token:wrong-kind";
        return "lookup:" + parsed.lookup_id;
    } catch (...) {
        return "token:malformed";
    }
}

[[nodiscard]] std::string clientIp(const Session& session) {
    return session.ipAddress.empty() ? "unknown" : session.ipAddress;
}

[[nodiscard]] std::string shareSessionComponent(const Session& session, const nlohmann::json& payload) {
    if (!session.shareSessionId().empty()) return "session:" + session.shareSessionId();

    const auto sessionId = optionalString(payload, "session_id");
    if (!sessionId.empty()) return "session:" + sessionId;

    const auto sessionTokenLookup = tokenLookupKey(payload, "session_token", TokenKind::ShareSession);
    if (sessionTokenLookup != "token:none") return sessionTokenLookup;

    return tokenLookupKey(payload, "public_token", TokenKind::PublicShare);
}

[[nodiscard]] std::string keyFor(
    const std::string_view command,
    const nlohmann::json& message,
    const Session& session
) {
    const auto& payload = payloadOf(message);
    const auto ip = clientIp(session);

    if (command == "share.session.open") {
        return std::format("{}|ip:{}|{}", command, ip, tokenLookupKey(payload, "public_token", TokenKind::PublicShare));
    }

    if (command == "share.email.challenge.start") {
        return std::format("{}|ip:{}|{}", command, ip, shareSessionComponent(session, payload));
    }

    if (command == "share.email.challenge.confirm") {
        const auto challengeId = optionalString(payload, "challenge_id");
        return std::format("{}|ip:{}|{}|challenge:{}", command, ip, shareSessionComponent(session, payload),
                           challengeId.empty() ? "none" : challengeId);
    }

    if (command == "share.download.chunk") {
        return std::format("{}|ip:{}|session:{}", command, ip,
                           session.shareSessionId().empty() ? "unknown" : session.shareSessionId());
    }

    if (command == "share.fs.metadata" ||
        command == "share.fs.list" ||
        command == "share.preview.get" ||
        command == "share.download.start" ||
        command == "share.upload.start") {
        const auto principal = session.sharePrincipal();
        const auto share = principal && !principal->share_id.empty() ? principal->share_id : "unknown";
        const auto shareSession = !session.shareSessionId().empty() ? session.shareSessionId() : "unknown";
        return std::format("{}|ip:{}|share:{}|session:{}", command, ip, share, shareSession);
    }

    return std::format("{}|ip:{}", command, ip);
}
}

vh::share::RateLimitDecision ShareRateLimit::check(
    const std::string_view command,
    const json& message,
    const Session& session,
    const Clock::time_point now
) {
    const auto policy = policyFor(command);
    if (!policy) return {.allowed = true, .remaining = 0, .retry_after = std::chrono::seconds{0}};
    return limiter_.check(keyFor(command, message, session), *policy, now);
}

void ShareRateLimit::reset() {
    limiter_.reset();
}

std::size_t ShareRateLimit::bucketCount() const {
    return limiter_.bucketCount();
}

bool ShareRateLimit::isLimitedCommand(const std::string_view command) {
    return policyFor(command).has_value();
}

}
