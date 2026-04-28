#pragma once

#include "share/Grant.hpp"

#include <ctime>
#include <optional>
#include <string>

namespace vh::share {

struct Principal {
    std::string share_id;
    std::string share_session_id;
    uint32_t vault_id{};
    uint32_t root_entry_id{};
    std::string root_path{"/"};
    Grant grant;
    bool email_verified{};
    std::time_t expires_at{};
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;
};

}
