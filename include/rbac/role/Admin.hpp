#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/permission/admin/*.hpp"

#include <string>
#include <optional>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::role {

struct Admin final : Meta {
    std::optional<uint32_t> user_id;

    permission::admin::Identities identities;
    permission::admin::Vaults vaults;
    permission::admin::Audits audits;
    permission::admin::Settings settings;
    permission::admin::Roles roles;
    permission::admin::Keys keys;
    permission::admin::VaultGlobals vGlobals;

    Admin() = default;
    Admin(const pqxx::row& row, const pqxx::result& globalVaultRoles);
    explicit Admin(const pqxx::row& row);
    explicit Admin(const nlohmann::json& j);

    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] std::string toString() const { return toString(0); }
    [[nodiscard]] std::string toFlagsString() const;

    static Admin fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Admin& a);
void from_json(const nlohmann::json& j, Admin& a);

std::vector<Admin> admin_roles_from_pq_res(const pqxx::result& res);
void to_json(nlohmann::json& j, const std::vector<Admin>& roles);
void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Admin>>& roles);

std::string to_string(const Admin& r);

}
