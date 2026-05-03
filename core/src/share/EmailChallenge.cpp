#include "share/EmailChallenge.hpp"

#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"
#include "share/Token.hpp"

#include <pqxx/row>
#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

using namespace vh::db::encoding;

namespace vh::share {
namespace email_challenge_model_detail {
std::optional<std::time_t> opt_time(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return parsePostgresTimestamp(row[column].as<std::string>());
}

std::optional<std::string> opt_string(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<std::string>();
}
}

EmailChallenge::EmailChallenge(const pqxx::row& row)
    : id(row["id"].as<std::string>()),
      share_id(row["share_id"].as<std::string>()),
      share_session_id(email_challenge_model_detail::opt_string(row, "share_session_id")),
      email_hash(from_hex_bytea(row["email_hash"].as<std::string>())),
      code_hash(from_hex_bytea(row["code_hash"].as<std::string>())),
      attempts(row["attempts"].as<uint32_t>()),
      max_attempts(row["max_attempts"].as<uint32_t>()),
      expires_at(parsePostgresTimestamp(row["expires_at"].as<std::string>())),
      consumed_at(email_challenge_model_detail::opt_time(row, "consumed_at")),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      ip_address(email_challenge_model_detail::opt_string(row, "ip_address")),
      user_agent(email_challenge_model_detail::opt_string(row, "user_agent")) {}

std::string EmailChallenge::normalizeEmail(const std::string_view email) {
    auto first = email.find_first_not_of(" \t\r\n");
    auto last = email.find_last_not_of(" \t\r\n");
    if (first == std::string_view::npos) throw std::invalid_argument("Email is required");
    std::string normalized(email.substr(first, last - first + 1));
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized.find('@') == std::string::npos) throw std::invalid_argument("Email is invalid");
    return normalized;
}

std::vector<uint8_t> EmailChallenge::hashEmail(const std::string_view normalized_email) {
    return Token::hashForDomain("vh/share-email/v1", normalized_email);
}

std::string EmailChallenge::generateCode() {
    if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
    std::ostringstream out;
    out << std::setw(6) << std::setfill('0') << randombytes_uniform(1000000);
    return out.str();
}

std::vector<uint8_t> EmailChallenge::hashCode(const std::string_view code) {
    return Token::hashForDomain("vh/share-email-code/v1", code);
}

bool EmailChallenge::verifyCode(const std::vector<uint8_t>& expected_hash, const std::string_view code) {
    const auto actual = hashCode(code);
    if (actual.size() != expected_hash.size()) return false;
    return sodium_memcmp(actual.data(), expected_hash.data(), actual.size()) == 0;
}

bool EmailChallenge::isExpired(const std::time_t now) const { return expires_at <= now; }

bool EmailChallenge::canAttempt(const std::time_t now) const {
    return !consumed_at && !isExpired(now) && attempts < max_attempts;
}

}
