#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::share {

struct Session {
    std::string id;
    std::string share_id;
    std::string session_token_lookup_id;
    std::vector<uint8_t> session_token_hash;
    std::optional<std::vector<uint8_t>> email_hash;
    std::optional<std::time_t> verified_at;
    std::time_t created_at{};
    std::time_t last_seen_at{};
    std::time_t expires_at{};
    std::optional<std::time_t> revoked_at;
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;

    Session() = default;
    explicit Session(const pqxx::row& row);

    [[nodiscard]] bool isExpired(std::time_t now) const;
    [[nodiscard]] bool isRevoked() const;
    [[nodiscard]] bool isVerified() const;
    [[nodiscard]] bool isActive(std::time_t now) const;
};

void to_json(nlohmann::json& j, const Session& session);

}
