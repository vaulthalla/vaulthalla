#include "rbac/permission/vault/APIKey.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault {

std::string APIKey::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "APIKey:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "View Secret: " << bool_to_string(canViewSecret()) << "\n";
    oss << in << "Modify: " << bool_to_string(canModify()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const APIKey& k) {
    j = {
        {"view", k.canView()},
        {"view_secret", k.canViewSecret()},
        {"modify", k.canModify()}
    };
}

void from_json(const nlohmann::json& j, APIKey& k) {
    k.permissions = 0;
    if (j.at("view").get<bool>()) k.permissions |= static_cast<APIKey::Mask>(APIKeyPermissions::View);
    if (j.at("view_secret").get<bool>()) k.permissions |= static_cast<APIKey::Mask>(APIKeyPermissions::ViewSecret);
    if (j.at("modify").get<bool>()) k.permissions |= static_cast<APIKey::Mask>(APIKeyPermissions::Modify);
}

}
