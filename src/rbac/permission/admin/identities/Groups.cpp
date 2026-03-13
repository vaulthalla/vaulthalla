#include "rbac/permission/admin/identities/Groups.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

void to_json(nlohmann::json& j, const Groups& o) {
    j = {
        {
            "groups", {
                {"view", o.canView()},
                {"edit", o.canEdit()},
                {"delete", o.canDelete()},
                {"add", o.canAdd()},
                {"add_member", o.canAddMember()},
                {"remove_member", o.canRemoveMember()},
            },
        }};
}

void from_json(const nlohmann::json& j, Groups& o) {
    o.permissions = 0;
    const auto& p = j.at("groups");
    if (p.at("view").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::View);
    if (p.at("edit").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::Edit);
    if (p.at("delete").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::Delete);
    if (p.at("add").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::Add);
    if (p.at("add_member").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::AddMember);
    if (p.at("remove_member").get<bool>()) o.permissions |= static_cast<Groups::Mask>(GroupPermissions::RemoveMember);
}

}
