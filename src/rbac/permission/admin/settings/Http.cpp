#include "rbac/permission/admin/settings/Http.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Http& s) {
    j = {{"http", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Http& s) {
    s = j.at("http").get<Base>();
}

}
