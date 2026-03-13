#include "rbac/permission/admin/settings/Auth.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Auth& s) {
    j = {{"auth", static_cast<Base>(s) }};
}

void from_json(const nlohmann::json& j, Auth& s) {
    s = j.at("auth").get<Base>();
}

}
