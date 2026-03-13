#include "rbac/permission/admin/settings/Websocket.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Websocket& s) {
    j = {{"websocket", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Websocket& s) {
    s = j.at("websocket").get<Base>();
}

}
