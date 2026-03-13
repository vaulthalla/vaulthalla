#include "rbac/permission/admin/identities/Base.hpp"

#include <nlohmann/json.hpp>

#include "protocols/ws/handler/Permissions.hpp"

namespace vh::rbac::permission::admin::identities {

void to_json(nlohmann::json& j, const IdentitiesBase& p) {
    j = {
        {"view", p.canView()},
        {"edit", p.canEdit()},
        {"add", p.canAdd()},
        {"delete", p.canDelete()}
    };
}

void from_json(const nlohmann::json& j, IdentitiesBase& p) {
    p.permissions = 0;
    if (j.at("view").get<bool>()) p.permissions |= static_cast<IdentitiesBase::Mask>(IdentityPermissions::View);
    if (j.at("edit").get<bool>()) p.permissions |= static_cast<IdentitiesBase::Mask>(IdentityPermissions::Edit);
    if (j.at("add").get<bool>()) p.permissions |= static_cast<IdentitiesBase::Mask>(IdentityPermissions::Add);
    if (j.at("delete").get<bool>()) p.permissions |= static_cast<IdentitiesBase::Mask>(IdentityPermissions::Delete);
}

}
