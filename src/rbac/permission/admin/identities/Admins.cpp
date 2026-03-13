#include "rbac/permission/admin/identities/Admins.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

void to_json(nlohmann::json& j, const Admins& admins) {
    j = {{"admins", static_cast<IdentitiesBase>(admins)}};
}

void from_json(const nlohmann::json& j, Admins& admins) {
    admins = j.at("admins").get<IdentitiesBase>();
}

}
