#include "rbac/permission/Admin.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>

namespace vh::rbac::permission {

Admin::Admin(const pqxx::row& row, const pqxx::result& vaultGlobalPerms)
    : identities(row["identities_permissions"].as<typename decltype(identities)::Mask>()),
      vaults(vaultGlobalPerms),
      audits(row["audits_permissions"].as<typename decltype(audits)::Mask>()),
      settings(row["settings_permissions"].as<typename decltype(settings)::Mask>()) {}

}
