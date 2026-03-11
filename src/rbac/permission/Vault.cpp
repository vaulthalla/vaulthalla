#include "rbac/permission/Vault.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>

namespace vh::rbac::permission {

Vault::Vault(const pqxx::row& row)
    : keys(row["keys_permissions"].as<typename decltype(keys)::Mask>()),
      roles(row["roles_permissions"].as<typename decltype(roles)::Mask>()),
      sync(row["sync_permissions"].as<typename decltype(sync)::Mask>()),
      filesystem(row) {}

}
