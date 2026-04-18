#include "rbac/permission/vault/Sync.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault {

std::string Sync::toFlagsString() const {
    return joinFlags(action, config);
}

std::string Sync::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Sync:\n";
    const auto i = indent + 2;
    oss << config.toString(i);
    oss << action.toString(i);
    return oss.str();
}

void to_json(nlohmann::json& j, const Sync& v) {
    j = {
        {"config", v.config},
        {"action", v.action}
    };
}

void from_json(const nlohmann::json& j, Sync& v) {
    j.at("config").get_to(v.config);
    j.at("action").get_to(v.action);
}

}
