#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include "protocols/ws/Fwd.hpp"

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Settings {
    static json get(const std::shared_ptr<Session>& session);
    // The operator policy any signed-in user's console needs (which share links may be made, vault defaults).
    // Read-only and not secret, unlike the full settings document (super admin only).
    static json policy(const std::shared_ptr<Session>& session);
    static json update(const json& payload, const std::shared_ptr<Session>& session);
};

}
