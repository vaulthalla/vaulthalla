#pragma once

#include "nlohmann/json_fwd.hpp"
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Stats {
    static json vault(const json& payload, const std::shared_ptr<Session>& session);
    static json fsCache(const std::shared_ptr<Session>& session);
    static json httpCache(const std::shared_ptr<Session>& session);
};

}
