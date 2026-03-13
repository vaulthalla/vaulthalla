#include "rbac/permission/admin/identities/Users.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

std::string Users::toString(const uint8_t indent) const {
    return "Users:\n" + std::string(indent, ' ') + static_cast<Base>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Users& u) {
    j = {{"users", static_cast<Base>(u)}};
}

void from_json(const nlohmann::json& j, Users& u) {
    u = j.at("users").get<Base>();
}

}
