#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::settings {

std::string Base::toString(const uint8_t indent) const {
    std::ostringstream oss;
    const auto in = std::string(indent, ' ');
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
    return oss.str();
}

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
