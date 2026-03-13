#include "rbac/permission/admin/settings/Services.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Services& s) {
    j = {{"services", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Services& s) {
    s = j.at("services").get<Base>();
}

}
