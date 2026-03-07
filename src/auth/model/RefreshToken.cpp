#include "auth/model/RefreshToken.hpp"

#include "db/encoding/timestamp.hpp"

#include <chrono>
#include <pqxx/row>
#include <utility>

using namespace vh::auth::model;

using namespace vh::db::encoding;

RefreshToken::RefreshToken(std::string jti,
                           std::string rawToken,
                           std::string hashedToken,
                           const uint32_t userId,
                           std::string userAgent,
                           std::string ipAddress)
    : Token(std::move(rawToken), userId),
      jti(std::move(jti)),
      hashedToken(std::move(hashedToken)),
      userAgent(std::move(userAgent)),
      ipAddress(std::move(ipAddress)),
      createdAt(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())),
      lastUsed(createdAt) {
    expiresAt = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + std::chrono::hours(24 * 7));
}

RefreshToken::RefreshToken(std::string rawToken) : Token(std::move(rawToken)) {}

RefreshToken::RefreshToken(const pqxx::row& row)
    : Token("", row["user_id"].as<uint32_t>()),
      jti(row["jti"].as<std::string>()),
      hashedToken(row["token_hash"].as<std::string>()),
      userAgent(row["user_agent"].as<std::string>()),
      ipAddress(row["ip_address"].as<std::string>()),
      createdAt(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      lastUsed(parsePostgresTimestamp(row["last_used"].as<std::string>())) {
    rawToken = !row["token"].is_null() ? row["token"].as<std::string>() : "";
    expiresAt = parsePostgresTimestamp(row["expires_at"].as<std::string>());
    revoked = row["revoked"].as<bool>();
}

bool RefreshToken::isValid() const { return !hashedToken.empty() && !revoked && !isExpired(); }

std::shared_ptr<RefreshToken> RefreshToken::fromIssuedToken(std::string jti, std::string rawToken,
                                                            std::string hashedToken, std::uint32_t userId,
                                                            std::string userAgent, std::string ipAddress) {
    return std::make_shared<RefreshToken>(std::move(jti), std::move(rawToken), std::move(hashedToken), userId,
                                          std::move(userAgent), std::move(ipAddress));
}