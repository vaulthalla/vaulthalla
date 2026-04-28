#include "share/Session.hpp"

#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>

using namespace vh::db::encoding;

namespace vh::share {
namespace session_model_detail {
std::optional<std::time_t> opt_time(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return parsePostgresTimestamp(row[column].as<std::string>());
}

std::optional<std::string> opt_string(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<std::string>();
}
}

Session::Session(const pqxx::row& row)
    : id(row["id"].as<std::string>()),
      share_id(row["share_id"].as<std::string>()),
      session_token_lookup_id(row["session_token_lookup_id"].as<std::string>()),
      session_token_hash(from_hex_bytea(row["session_token_hash"].as<std::string>())),
      email_hash(row["email_hash"].is_null()
                     ? std::nullopt
                     : std::make_optional(from_hex_bytea(row["email_hash"].as<std::string>()))),
      verified_at(session_model_detail::opt_time(row, "verified_at")),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      last_seen_at(parsePostgresTimestamp(row["last_seen_at"].as<std::string>())),
      expires_at(parsePostgresTimestamp(row["expires_at"].as<std::string>())),
      revoked_at(session_model_detail::opt_time(row, "revoked_at")),
      ip_address(session_model_detail::opt_string(row, "ip_address")),
      user_agent(session_model_detail::opt_string(row, "user_agent")) {}

bool Session::isExpired(const std::time_t now) const { return expires_at <= now; }
bool Session::isRevoked() const { return revoked_at.has_value(); }
bool Session::isVerified() const { return verified_at.has_value(); }
bool Session::isActive(const std::time_t now) const { return !isExpired(now) && !isRevoked(); }

void to_json(nlohmann::json& j, const Session& session) {
    j = {
        {"id", session.id},
        {"share_id", session.share_id},
        {"session_token_lookup_id", session.session_token_lookup_id},
        {"verified_at", session.verified_at ? nlohmann::json(timestampToString(*session.verified_at)) : nlohmann::json(nullptr)},
        {"created_at", timestampToString(session.created_at)},
        {"last_seen_at", timestampToString(session.last_seen_at)},
        {"expires_at", timestampToString(session.expires_at)},
        {"revoked_at", session.revoked_at ? nlohmann::json(timestampToString(*session.revoked_at)) : nlohmann::json(nullptr)},
        {"ip_address", session.ip_address},
        {"user_agent", session.user_agent}
    };
}

}
