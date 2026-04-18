#include "auth/model/RefreshToken.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/ws/Session.hpp"
#include "auth/model/TokenPair.hpp"
#include "log/Registry.hpp"

#include <pqxx/row>
#include <utility>

using namespace vh::db::encoding;

namespace vh::auth::model {

RefreshToken::RefreshToken(const pqxx::row& row)
    : Token(row),
      hashedToken(row["token_hash"].as<std::string>()),
      userAgent(row["user_agent"].as<std::string>()),
      ipAddress(row["ip_address"].as<std::string>()),
      lastUsed(std::chrono::system_clock::from_time_t(parsePostgresTimestamp(row["last_used"].as<std::string>()))) {}

RefreshToken::RefreshToken(std::string rawToken) : Token(std::move(rawToken)) {}

bool RefreshToken::isValid() const {
    if (hashedToken.empty()) {
        log::Registry::auth()->debug("[RefreshToken] No hashed token, invalid");
        return false;
    }

    if (revoked) {
        log::Registry::auth()->debug("[RefreshToken] Token is revoked");
        return false;
    }

    if (isExpired()) {
        log::Registry::auth()->debug("[RefreshToken] Token is expired");
        return false;
    }

    return true;
}

bool RefreshToken::dangerousDivergence(const std::shared_ptr<RefreshToken>& other) const {
    if (!other) return false; // No divergence if the other token doesn't exist
    return hashedToken == other->hashedToken && (
        jti != other->jti || userAgent != other->userAgent ||
        ipAddress != other->ipAddress || issuedAt != other->issuedAt || expiresAt != other->expiresAt ||
        lastUsed != other->lastUsed || revoked != other->revoked
    );
}

void RefreshToken::hardInvalidate() {
    revoke();
    db::query::auth::RefreshToken::refresh(jti);
    hashedToken.clear();
    rawToken.clear();
}

void RefreshToken::addToSession(const std::shared_ptr<protocols::ws::Session>& session, std::string token) {
    session->tokens->refreshToken = std::make_shared<RefreshToken>(std::move(token));
}

bool operator==(const std::shared_ptr<RefreshToken>& lhs, const std::shared_ptr<RefreshToken>& rhs) {
    return lhs->revoked == rhs->revoked && lhs->jti == rhs->jti && lhs->hashedToken == rhs->hashedToken &&
           lhs->userAgent == rhs->userAgent && lhs->ipAddress == rhs->ipAddress &&
           lhs->issuedAt == rhs->issuedAt && lhs->expiresAt == rhs->expiresAt;
}

}
