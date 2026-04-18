#include "rbac/permission/admin/keys/Encryption.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::keys {
    std::string EncryptionKey::toString(const uint8_t indent) const {
        std::ostringstream oss;
        oss << std::string(indent, ' ') << "Encryption Key:\n";
        const std::string in(indent + 2, ' ');
        oss << in << "View: " << bool_to_string(canView()) << "\n";
        oss << in << "Export: " << bool_to_string(canExport()) << "\n";
        oss << in << "Rotate: " << bool_to_string(canRotate()) << "\n";
        return oss.str();
    }

    void to_json(nlohmann::json &j, const EncryptionKey &k) {
        j = {
            {"view", k.canView()},
            {"export", k.canExport()},
            {"rotate", k.canRotate()}
        };
    }

    void from_json(const nlohmann::json &j, EncryptionKey &k) {
        k.clear();
        if (j.at("view").get<bool>()) k.grant(EncryptionKeyPermissions::View);
        if (j.at("export").get<bool>()) k.grant(EncryptionKeyPermissions::Export);
        if (j.at("rotate").get<bool>()) k.grant(EncryptionKeyPermissions::Rotate);
    }
}
