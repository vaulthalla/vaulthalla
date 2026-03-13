#include "rbac/permission/admin/settings/Logging.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Logging& s) {
    j = {{"logging", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Logging& s) {
    s = j.at("logging").get<Base>();
}

}
