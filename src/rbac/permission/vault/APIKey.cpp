#include "rbac/permission/vault/APIKey.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

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
