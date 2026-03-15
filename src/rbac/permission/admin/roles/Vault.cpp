#include "rbac/permission/admin/roles/Vault.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::roles {

    std::string Vault::toString(const uint8_t indent) const {
        std::string result = std::string(indent, ' ') + "Vault:\n";
        result += Base::toString(indent + 2);
        return result;
    }

    void to_json(nlohmann::json &j, const Vault &admin) {
        j = {{ "vault", static_cast<const Base&>(admin) }};
    }

    void from_json(const nlohmann::json& j, Vault& admin) {
        j.at("vault").get_to(static_cast<Base&>(admin));
    }

}
