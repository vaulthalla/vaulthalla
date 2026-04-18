#include "rbac/permission/admin/Keys.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin {
    uint32_t Keys::toMask() const { return pack(apiKeys.self, apiKeys.admin, apiKeys.user, encryptionKeys); }
    void Keys::fromMask(const Mask mask) { unpack(mask, apiKeys.self, apiKeys.admin, apiKeys.user, encryptionKeys); }

    std::string Keys::toFlagsString() const {
        return apiKeys.toFlagsString() + " " + encryptionKeys.toFlagsString();
    }

    std::string Keys::toString(const uint8_t indent) const {
        std::ostringstream oss;
        oss << std::string(indent, ' ') << "Keys:\n";
        const auto i = indent + 2;
        oss << apiKeys.toString(i);
        oss << encryptionKeys.toString(i);
        return oss.str();
    }

    void to_json(nlohmann::json &j, const Keys &k) {
        j = {
            {"api_keys", k.apiKeys},
            {"encryption_keys", k.encryptionKeys}
        };
    }

    void from_json(const nlohmann::json &j, Keys &k) {
        j.at("api_keys").get_to(k.apiKeys);
        j.at("encryption_keys").get_to(k.encryptionKeys);
    }
}
