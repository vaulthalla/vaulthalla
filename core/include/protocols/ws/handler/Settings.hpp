#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Settings {
    static json get(const std::shared_ptr<Session>& session);
    static json update(const json& payload, const std::shared_ptr<Session>& session);
};

}
