#include "rbac/permission/admin/identities/Users.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

std::string Users::toString(const uint8_t indent) const {
    return Base::toString(indent);
}

void to_json(nlohmann::json& j, const Users& u) {
    j = {{"users", static_cast<const Base&>(u)}};
}

void from_json(const nlohmann::json& j, Users& u) {
    j.at("users").get_to(static_cast<Base&>(u));
}

}
