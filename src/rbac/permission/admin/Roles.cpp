#include "rbac/permission/admin/Roles.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin {

    std::string Roles::toString(const uint8_t indent) const {
        return "Roles: " + std::string(indent, ' ') + "\n" + admin.toString(indent + 2) + vault.toString(indent + 2);
    }

    std::string Roles::toFlagsString() const {
        return admin.toFlagsString() + " " + vault.toFlagsString();
    }

    void to_json(nlohmann::json &j, const Roles& r) {
        j = {{"admin", r.admin }, { "vault", r.vault }};
    }

    void from_json(const nlohmann::json& j, Roles& r) {
        j.at("admin").get_to(r.admin);
        j.at("vault").get_to(r.vault);
    }

}
