#include "rbac/permission/admin/settings/Database.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Database::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "DB:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Database& s) {
    j = {{"database", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Database& s) {
    j.at("database").get_to(static_cast<Base&>(s));
}

}
