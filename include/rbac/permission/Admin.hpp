#pragma once

#include "rbac/permission/admin/Identities.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/permission/admin/Audits.hpp"
#include "rbac/permission/admin/Settings.hpp"
#include "rbac/permission/admin/Roles.hpp"

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }

namespace vh::rbac::permission {

struct Admin {
    admin::Identities identities;
    admin::Vaults vaults;
    admin::Audits audits;
    admin::Settings settings;
    admin::Roles roles;

    Admin() = default;
    Admin(const pqxx::row& row, const pqxx::result& vaultGlobalPerms);

    [[nodiscard]] std::string toString(uint8_t indent) const;
    [[nodiscard]] std::string toFlagsString() const;
};

void to_json(nlohmann::json& j, const Admin& a);
void from_json(const nlohmann::json& j, Admin& a);

}
