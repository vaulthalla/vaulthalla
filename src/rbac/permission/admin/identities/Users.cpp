#include "rbac/permission/admin/identities/Users.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

void to_json(nlohmann::json& j, const Users& u) {
    j = {{"users", static_cast<IdentitiesBase>(u)}};
}

void from_json(const nlohmann::json& j, Users& u) {
    u = j.at("users").get<IdentitiesBase>();
}

}
