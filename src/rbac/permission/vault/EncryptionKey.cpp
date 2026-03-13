#include "rbac/permission/vault/EncryptionKey.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

void to_json(nlohmann::json& j, const EncryptionKey& k) {
    j = {
        {"view", k.canView()},
        {"export", k.canExport()},
        {"rotate", k.canRotate()}
    };
}

void from_json(const nlohmann::json& j, EncryptionKey& k) {
    k.clear();
    if (j.at("view").get<bool>()) k.grant(EncryptionKeyPermissions::View);
    if (j.at("export").get<bool>()) k.grant(EncryptionKeyPermissions::Export);
    if (j.at("rotate").get<bool>()) k.grant(EncryptionKeyPermissions::Rotate);
}

}
