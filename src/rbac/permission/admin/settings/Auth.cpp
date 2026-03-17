#include "rbac/permission/admin/settings/Auth.hpp"
#include <fmt/core.h>

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Auth::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "Auth:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Auth& s) {
    j = {{"auth", static_cast<const Base&>(s) }};
}

void from_json(const nlohmann::json& j, Auth& s) {
    j.at("auth").get_to(static_cast<Base&>(s));
}

}
