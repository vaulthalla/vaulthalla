#include "rbac/permission/vault/Roles.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

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
