#include "rbac/permission/admin/settings/Sharing.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Sharing& s) {
    j = {{"sharing", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Sharing& s) {
    s = j.at("sharing").get<Base>();
}

}
