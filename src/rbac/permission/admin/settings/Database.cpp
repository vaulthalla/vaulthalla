#include "rbac/permission/admin/settings/Database.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

void to_json(nlohmann::json& j, const Database& s) {
    j = {{"database", static_cast<Base>(s)}};
}

void from_json(const nlohmann::json& j, Database& s) {
    s = Database(j.at("database").get<Base>());
}

}
