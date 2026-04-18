#include "rbac/permission/admin/roles/Base.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::roles {

    std::string Base::toString(const uint8_t indent) const {
        std::ostringstream oss;
        const std::string in(indent, ' ');
        oss << in << "View: " << bool_to_string(canView()) << "\n";
        oss << in << "Add: " << bool_to_string(canAdd()) << "\n";
        oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
        oss << in << "Delete: " << bool_to_string(canDelete()) << "\n";
        return oss.str();
    }

    void to_json(nlohmann::json &j, const Base &p) {
        j = {
            {"view", p.canView()},
            {"add", p.canAdd()},
            {"edit", p.canEdit()},
            {"delete", p.canDelete()}
        };
    }

    void from_json(const nlohmann::json &j, Base &p) {
        p.clear();
        if (j.at("view").get<bool>()) p.grant(RolesPermissions::View);
        if (j.at("add").get<bool>()) p.grant(RolesPermissions::Add);
        if (j.at("edit").get<bool>()) p.grant(RolesPermissions::Edit);
        if (j.at("delete").get<bool>()) p.grant(RolesPermissions::Delete);
    }
}
