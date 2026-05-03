#pragma once

#include "share/Types.hpp"

#include <ctime>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::share {

struct AuditEvent {
    uint64_t id{};
    std::optional<std::string> share_id;
    std::optional<std::string> share_session_id;
    AuditActorType actor_type{AuditActorType::Unknown};
    std::optional<uint32_t> actor_user_id;
    std::string event_type;
    std::optional<uint32_t> vault_id;
    std::optional<uint32_t> target_entry_id;
    std::optional<std::string> target_path;
    AuditStatus status{AuditStatus::Success};
    std::optional<uint64_t> bytes_transferred;
    std::optional<std::string> error_code;
    std::optional<std::string> error_message;
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;
    std::time_t created_at{};

    AuditEvent() = default;
    explicit AuditEvent(const pqxx::row& row);

    [[nodiscard]] nlohmann::json toRedactedJson() const;
};

void to_json(nlohmann::json& j, const AuditEvent& event);

}
