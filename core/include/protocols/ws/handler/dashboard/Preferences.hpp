#pragma once

#include "nlohmann/json_fwd.hpp"

#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler::dashboard {

using json = nlohmann::json;

struct Preferences {
    static json get(const json& payload, const std::shared_ptr<Session>& session);
    static json update(const json& payload, const std::shared_ptr<Session>& session);
    static json reset(const json& payload, const std::shared_ptr<Session>& session);
};

}
