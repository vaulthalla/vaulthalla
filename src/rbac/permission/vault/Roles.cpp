#include "rbac/permission/vault/Roles.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault {

std::string Roles::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Roles:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "Assign: " << bool_to_string(canAssign()) << "\n";
    oss << in << "Modify: " << bool_to_string(canModify()) << "\n";
    oss << in << "Revoke: " << bool_to_string(canRevoke()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const Roles& r) {
    j = {
        {"assign", r.canAssign()},
        {"modify", r.canModify()},
        {"revoke", r.canRevoke()}
    };
}

void from_json(const nlohmann::json& j, Roles& r) {
    r.clear();
    if (j.at("assign").get<bool>()) r.grant(RolePermissions::Assign);
    if (j.at("modify").get<bool>()) r.grant(RolePermissions::Modify);
    if (j.at("revoke").get<bool>()) r.grant(RolePermissions::Revoke);
}

}
