#include "rbac/permission/vault/sync/Action.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault::sync {

std::string Action::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Action:\n";
    const auto in = std::string(indent + 2, ' ');
    oss << in << "Trigger Sync: " << bool_to_string(canTrigger()) << "\n";
    oss << in << "Sign Upstream Encryption Waivers: " << bool_to_string(canSignWaiver()) << "\n";
    return oss.str();
}


void to_json(nlohmann::json& j, const Action& a) {
    j = {
        {"trigger", a.canTrigger()},
        {"sign_waiver", a.canSignWaiver()}
    };
}

void from_json(const nlohmann::json& j, Action& a) {
    a.clear();
    if (j.at("trigger").get<bool>()) a.grant(SyncActionPermissions::Trigger);
    if (j.at("sign_waiver").get<bool>()) a.grant(SyncActionPermissions::SignWaiver);
}

}
