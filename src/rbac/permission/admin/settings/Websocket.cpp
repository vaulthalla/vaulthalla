#include "rbac/permission/admin/settings/Websocket.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Websocket::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "WebSocket:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Websocket& s) {
    j = {{"websocket", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Websocket& s) {
    j.at("websocket").get_to(static_cast<Base&>(s));
}

}
