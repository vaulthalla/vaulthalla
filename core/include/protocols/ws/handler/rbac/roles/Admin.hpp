#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include "protocols/ws/Fwd.hpp"

namespace vh::protocols::ws::handler::rbac::roles {

using json = nlohmann::json;

struct Admin {
    static json add(const json& payload, const std::shared_ptr<Session>& session);
    static json remove(const json& payload, const std::shared_ptr<Session>& session);
    static json update(const json& payload, const std::shared_ptr<Session>& session);
    static json get(const json& payload, const std::shared_ptr<Session>& session);
    static json getByName(const json& payload, const std::shared_ptr<Session>& session);
    static json list(const std::shared_ptr<Session>& session);
};

}
