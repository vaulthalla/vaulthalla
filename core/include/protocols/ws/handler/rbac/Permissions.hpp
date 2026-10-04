#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include "protocols/ws/Fwd.hpp"

namespace vh::protocols::ws::handler::rbac {

using json = nlohmann::json;

struct Permissions {
    static json get(const json& payload);
    static json getByName(const json& payload);
    static json list();
};

}
