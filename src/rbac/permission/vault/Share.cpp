#include "rbac/permission/vault/Share.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

void to_json(nlohmann::json& j, const Share& s) {
    j = {
        {"internal", s.canShareInternally()},
        {"public", s.canSharePublicly()},
        {"public_with_validation", s.canSharePubliclyWithValidation()}
    };
}

void from_json(const nlohmann::json& j, Share& s) {
    s.clear();
    if (j.at("internal").get<bool>()) s.grant(SharePermissions::Internal);
    if (j.at("public").get<bool>()) s.grant(SharePermissions::Public);
    if (j.at("public_with_validation").get<bool>()) s.grant(SharePermissions::PublicWithValidation);
}

}
