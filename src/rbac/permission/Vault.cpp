#include "rbac/permission/Vault.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>

namespace vh::rbac::permission {

Vault::Vault(const pqxx::row& row)
    : keys(row["keys_permissions"].as<typename decltype(keys)::Mask>()),
      roles(row["roles_permissions"].as<typename decltype(roles)::Mask>()),
      sync(row["sync_permissions"].as<typename decltype(sync)::Mask>()),
      filesystem(row) {}

void to_json(nlohmann::json& j, const Vault& v) {
    j = nlohmann::json{
        {"keys", v.keys},
        {"roles", v.roles},
        {"sync", v.sync},
        {"filesystem", v.filesystem},
    };
}

void from_json(const nlohmann::json& j, Vault& v) {
    j.at("keys").get_to(v.keys);
    j.at("roles").get_to(v.roles);
    j.at("sync").get_to(v.sync);
    j.at("filesystem").get_to(v.filesystem);
}

}
