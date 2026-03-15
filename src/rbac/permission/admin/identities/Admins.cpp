#include "rbac/permission/admin/identities/Admins.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::identities {

std::string Admins::toString(const uint8_t indent) const {
    return "Admins:\n" + std::string(indent, ' ') + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Admins& admins) {
    j = {{"admins", static_cast<const Base&>(admins)}};
}

void from_json(const nlohmann::json& j, Admins& admins) {
    j.at("admins").get_to(static_cast<Base&>(admins));
}

}
