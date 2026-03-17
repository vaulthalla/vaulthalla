#include "rbac/permission/admin/settings/Http.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Http::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "HTTP:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Http& s) {
    j = {{"http", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Http& s) {
    j.at("http").get_to(static_cast<Base&>(s));
}

}
