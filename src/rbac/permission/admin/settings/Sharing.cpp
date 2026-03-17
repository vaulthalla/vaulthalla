#include "rbac/permission/admin/settings/Sharing.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::settings {

std::string Sharing::toString(const uint8_t indent) const {
    return std::string(indent, ' ') + "Sharing:\n" + static_cast<const Base&>(*this).toString(indent + 2);
}

void to_json(nlohmann::json& j, const Sharing& s) {
    j = {{"sharing", static_cast<const Base&>(s)}};
}

void from_json(const nlohmann::json& j, Sharing& s) {
    j.at("sharing").get_to(static_cast<Base&>(s));
}

}
