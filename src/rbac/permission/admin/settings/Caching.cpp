#include "rbac/permission/admin/settings/Caching.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Caching::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "Caching:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Caching& s) {
    j = {{"caching", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Caching& s) {
    j.at("caching").get_to(static_cast<Base&>(s));
}

}
