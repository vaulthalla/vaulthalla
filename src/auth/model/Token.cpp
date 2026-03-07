#include "auth/model/Token.hpp"

#include <pqxx/row>

#include "db/encoding/timestamp.hpp"

using namespace vh::auth::model;
using namespace vh::db::encoding;
using namespace std::chrono;

Token::Token(std::string token, const unsigned short userId)
    : rawToken(std::move(token)),
      userId(userId),
      expiresAt(system_clock::to_time_t(system_clock::now() + hours(1))) {}

Token::Token(std::string rawToken) : rawToken(std::move(rawToken)) {}

Token::Token(const pqxx::row& row)
    : rawToken(row["raw_token"].c_str()),
      userId(row["user_id"].as<unsigned short>()),
      expiresAt(parsePostgresTimestamp(row["expires_at"].as<std::string>())),
      revoked(row["revoked"].as<bool>()) {}

bool Token::isExpired() const { return system_clock::to_time_t(system_clock::now()) > expiresAt; }
bool Token::isValid() const { return !revoked && !isExpired(); }

seconds Token::timeRemaining() const {
    return duration_cast<seconds>(system_clock::from_time_t(expiresAt) - system_clock::now());
}

