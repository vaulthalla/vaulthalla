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
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Assign Overrides: " << bool_to_string(canAssignOverride()) << "\n";
    oss << in << "Modify Overrides: " << bool_to_string(canModifyOverride()) << "\n";
    oss << in << "Revoke Overrides: " << bool_to_string(canRevokeOverride()) << "\n";
    oss << in << "View Override: " << bool_to_string(canViewOverride()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const Roles& r) {
    j = {
        {"assign", r.canAssign()},
        {"modify", r.canModify()},
        {"revoke", r.canRevoke()},
        {"view", r.canView()},
        {"assign_override", r.canAssignOverride()},
        {"modify_override", r.canModifyOverride()},
        {"revoke_override", r.canRevokeOverride()},
        {"view_override", r.canViewOverride()}
    };
}

void from_json(const nlohmann::json& j, Roles& r) {
    r.clear();
    if (j.at("assign").get<bool>()) r.grant(RolePermissions::Assign);
    if (j.at("modify").get<bool>()) r.grant(RolePermissions::Modify);
    if (j.at("revoke").get<bool>()) r.grant(RolePermissions::Revoke);
    if (j.at("view").get<bool>()) r.grant(RolePermissions::View);
    if (j.at("assign_override").get<bool>()) r.grant(RolePermissions::AssignOverride);
    if (j.at("modify_override").get<bool>()) r.grant(RolePermissions::ModifyOverride);
    if (j.at("revoke_override").get<bool>()) r.grant(RolePermissions::RevokeOverride);
    if (j.at("view_override").get<bool>()) r.grant(RolePermissions::ViewOverride);
}

}
