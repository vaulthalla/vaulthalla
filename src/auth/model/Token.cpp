#include "auth/model/Token.hpp"
#include "db/encoding/timestamp.hpp"
#include "auth/session/Issuer.hpp"
#include "log/Registry.hpp"

#include <pqxx/row>

using namespace vh::auth::model;
using namespace vh::db::encoding;
using namespace std::chrono;

Token::Token(const pqxx::row& row)
    : jti(row["jti"].as<std::string>()),
      userId(row["user_id"].as<unsigned short>()),
      issuedAt(system_clock::from_time_t(parsePostgresTimestamp(row["issued_at"].as<std::string>()))),
      expiresAt(system_clock::from_time_t(parsePostgresTimestamp(row["expires_at"].as<std::string>()))),
      revoked(row["revoked"].as<bool>()) {}

Token::Token(std::string rawToken) : rawToken(std::move(rawToken)) {}

bool Token::isExpired() const { return system_clock::now() > expiresAt; }
bool Token::isValid() const {
    if (revoked) {
        log::Registry::auth()->debug("[Token] Token is revoked");
        return false;
    }

    if (isExpired()) {
        log::Registry::auth()->debug("[Token] Token is expired");
        return false;
    }

    return true;
}

seconds Token::timeRemaining() const {
    return duration_cast<seconds>(expiresAt - system_clock::now());
}

bool vh::auth::model::operator==(const std::shared_ptr<Token>& lhs, const std::shared_ptr<Token>& rhs) {
    return lhs->rawToken == rhs->rawToken && lhs->userId == rhs->userId && lhs->expiresAt == rhs->expiresAt &&
           lhs->revoked == rhs->revoked;
}

void Token::dangerousDivergence(const std::optional<session::TokenClaims>& claims) const {
    if (!claims) throw std::runtime_error("Token claims are required for dangerous diversion");
    if (claims->expiresAt < system_clock::now()) throw std::runtime_error("Token is expired");
    if (claims->subject.empty()) throw std::runtime_error("Token subject is empty");
    if (claims->jti.empty() || claims->jti != jti) throw std::runtime_error("Token jti is invalid");
}
