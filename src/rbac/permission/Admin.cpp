#include "rbac/permission/Admin.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>

namespace vh::rbac::permission {

Admin::Admin(const pqxx::row& row, const pqxx::result& vaultGlobalPerms)
    : identities(row["identities_permissions"].as<typename decltype(identities)::Mask>()),
      vaults(vaultGlobalPerms),
      audits(row["audits_permissions"].as<typename decltype(audits)::Mask>()),
      settings(row["settings_permissions"].as<typename decltype(settings)::Mask>()) {}

void to_json(nlohmann::json& j, const Admin& a) {
    j = nlohmann::json{
        {"identities", a.identities},
        {"vaults", a.vaults},
        {"audits", a.audits},
        {"settings", a.settings},
    };
}

void from_json(const nlohmann::json& j, Admin& a) {
    j.at("identities").get_to(a.identities);
    j.at("vaults").get_to(a.vaults);
    j.at("audits").get_to(a.audits);
    j.at("settings").get_to(a.settings);
}

}
