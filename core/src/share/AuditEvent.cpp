#include "share/AuditEvent.hpp"

#include "db/encoding/timestamp.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>

using namespace vh::db::encoding;

namespace vh::share {
namespace audit_model_detail {
std::optional<std::string> opt_string(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<std::string>();
}

template <typename T>
std::optional<T> opt_num(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<T>();
}
}

AuditEvent::AuditEvent(const pqxx::row& row)
    : id(row["id"].as<uint64_t>()),
      share_id(audit_model_detail::opt_string(row, "share_id")),
      share_session_id(audit_model_detail::opt_string(row, "share_session_id")),
      actor_type(audit_actor_type_from_string(row["actor_type"].as<std::string>())),
      actor_user_id(audit_model_detail::opt_num<uint32_t>(row, "actor_user_id")),
      event_type(row["event_type"].as<std::string>()),
      vault_id(audit_model_detail::opt_num<uint32_t>(row, "vault_id")),
      target_entry_id(audit_model_detail::opt_num<uint32_t>(row, "target_entry_id")),
      target_path(audit_model_detail::opt_string(row, "target_path")),
      status(audit_status_from_string(row["status"].as<std::string>())),
      bytes_transferred(audit_model_detail::opt_num<uint64_t>(row, "bytes_transferred")),
      error_code(audit_model_detail::opt_string(row, "error_code")),
      error_message(audit_model_detail::opt_string(row, "error_message")),
      ip_address(audit_model_detail::opt_string(row, "ip_address")),
      user_agent(audit_model_detail::opt_string(row, "user_agent")),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())) {}

nlohmann::json AuditEvent::toRedactedJson() const {
    return {
        {"id", id},
        {"share_id", share_id},
        {"share_session_id", share_session_id},
        {"actor_type", actor_type},
        {"actor_user_id", actor_user_id},
        {"event_type", event_type},
        {"vault_id", vault_id},
        {"target_entry_id", target_entry_id},
        {"target_path", target_path},
        {"status", status},
        {"bytes_transferred", bytes_transferred},
        {"error_code", error_code},
        {"error_message", error_message},
        {"ip_address", ip_address},
        {"user_agent", user_agent},
        {"created_at", timestampToString(created_at)}
    };
}

void to_json(nlohmann::json& j, const AuditEvent& event) { j = event.toRedactedJson(); }

}
