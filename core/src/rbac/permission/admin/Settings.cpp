#include "rbac/permission/admin/Settings.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

#include "protocols/http/Server.hpp"

namespace vh::rbac::permission::admin {

std::string Settings::toFlagsString() const {
    return joinFlags(websocket, http, database, auth, logging, caching, sharing, services);
}

std::string Settings::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Settings:\n";
    const auto i = indent + 2;
    oss << websocket.toString(i);
    oss << http.toString(i);
    oss << database.toString(i);
    oss << auth.toString(i);
    oss << logging.toString(i);
    oss << caching.toString(i);
    oss << sharing.toString(i);
    oss << services.toString(i);
    return oss.str();
}

void to_json(nlohmann::json& j, const Settings& o) {
    j = {
        {"websocket", o.websocket},
        {"http", o.http},
        {"database", o.database},
        {"auth", o.auth},
        {"logging", o.logging},
        {"caching", o.caching},
        {"sharing", o.sharing},
        {"services", o.services}
    };
}

void from_json(const nlohmann::json& j, Settings& o) {
    j.at("websocket").get_to(o.websocket);
    j.at("http").get_to(o.http);
    j.at("database").get_to(o.database);
    j.at("auth").get_to(o.auth);
    j.at("logging").get_to(o.logging);
    j.at("caching").get_to(o.caching);
    j.at("sharing").get_to(o.sharing);
    j.at("services").get_to(o.services);
}

}
