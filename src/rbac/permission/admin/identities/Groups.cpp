#include "rbac/permission/admin/identities/Groups.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::identities {

std::string Groups::toString(uint8_t indent) const {
    std::ostringstream oss;
    const auto in = std::string(indent + 2, ' ');
    oss << std::string(indent, ' ') << "Groups:" << "\n";
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
    oss << in << "Delete: " << bool_to_string(canDelete()) << "\n";
    oss << in << "Add: " << bool_to_string(canAdd()) << "\n";
    oss << in << "Add Member: " << bool_to_string(canAddMember()) << "\n";
    oss << in << "Remove Member: " << bool_to_string(canDelete()) << "\n";
    return oss.str();
}

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
