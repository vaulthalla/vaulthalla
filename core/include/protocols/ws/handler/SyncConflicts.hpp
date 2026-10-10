#pragma once

#include "nlohmann/json_fwd.hpp"
#include "protocols/ws/Fwd.hpp"

#include <memory>

// sync.conflicts.* (#187): thin adapters over ops::conflicts.
namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct SyncConflicts {
    static json summary(const std::shared_ptr<Session>& session);
    static json list(const json& payload, const std::shared_ptr<Session>& session);
    static json resolve(const json& payload, const std::shared_ptr<Session>& session);
};

}
