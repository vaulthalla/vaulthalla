#include "rbac/permission/admin/settings/Caching.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Caching& s) {
    j = {{"caching", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Caching& s) {
    s = j.at("caching").get<Base>();
}

}
