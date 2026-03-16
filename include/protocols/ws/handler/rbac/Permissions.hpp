#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler::rbac {

using json = nlohmann::json;

struct Permissions {
    static json get(const json& payload);
    static json getByName(const json& payload);
    static json list();
};

}
