#pragma once

#include <nlohmann/json_fwd.hpp>
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Roles {
    static json add(const json& payload, const std::shared_ptr<Session>& session);
    static json remove(const json& payload, const std::shared_ptr<Session>& session);
    static json update(const json& payload, const std::shared_ptr<Session>& session);
    static json get(const json& payload, const std::shared_ptr<Session>& session);
    static json getByName(const json& payload, const std::shared_ptr<Session>& session);

    static json list(const std::shared_ptr<Session>& session);
    static json listUserRoles(const std::shared_ptr<Session>& session);
    static json listVaultRoles(const std::shared_ptr<Session>& session);
};

}
