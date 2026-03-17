#include "rbac/permission/admin/settings/Logging.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Logging::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "Logging:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Logging& s) {
    j = {{"logging", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Logging& s) {
    j.at("logging").get_to(static_cast<Base&>(s));
}

}
