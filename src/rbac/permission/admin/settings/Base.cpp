#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Base& s) {
    j = {
        {"view", s.canView()},
        {"edit", s.canEdit()}
    };
}

void from_json(const nlohmann::json& j, Base& s) {
    s.permissions = 0;
    if (j.at("view").get<bool>()) s.permissions |= static_cast<Base::Mask>(SettingsPermissions::View);
    if (j.at("edit").get<bool>()) s.permissions |= static_cast<Base::Mask>(SettingsPermissions::Edit);
}

}
