#include "rbac/permission/admin/identities/Base.hpp"
#include "protocols/ws/handler/rbac/Permissions.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::identities {

std::string Base::toString(const uint8_t indent) const {
    std::ostringstream oss;
    const auto in = std::string(indent, ' ');
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
    oss << in << "Add: " << bool_to_string(canAdd()) << "\n";
    oss << in << "Delete: " << bool_to_string(canDelete()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const Base& p) {
    j = {
        {"view", p.canView()},
        {"edit", p.canEdit()},
        {"add", p.canAdd()},
        {"delete", p.canDelete()}
    };
}

void from_json(const nlohmann::json& j, Base& p) {
    p.permissions = 0;
    if (j.at("view").get<bool>()) p.permissions |= static_cast<Base::Mask>(IdentityPermissions::View);
    if (j.at("edit").get<bool>()) p.permissions |= static_cast<Base::Mask>(IdentityPermissions::Edit);
    if (j.at("add").get<bool>()) p.permissions |= static_cast<Base::Mask>(IdentityPermissions::Add);
    if (j.at("delete").get<bool>()) p.permissions |= static_cast<Base::Mask>(IdentityPermissions::Delete);
}

}
