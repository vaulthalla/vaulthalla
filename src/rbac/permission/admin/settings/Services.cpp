#include "rbac/permission/admin/settings/Services.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Services::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "Services:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Services& s) {
    j = {{"services", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Services& s) {
    j.at("services").get_to(static_cast<Base&>(s));
}

}
