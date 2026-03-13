#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/permission/Admin.hpp"

#include <string>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::role {

struct Admin final : Meta {
    std::optional<uint32_t> user_id;
    permission::Admin permissions{};

    Admin() = default;
    Admin(const pqxx::row& row, const pqxx::result& globalVaultRoles);
    explicit Admin(const pqxx::row& row);
    explicit Admin(const nlohmann::json& j);

    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] std::string toString() const { return toString(0); }

    static Admin fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Admin& a);
void from_json(const nlohmann::json& j, Admin& a);

std::vector<Admin> admin_roles_from_pq_res(const pqxx::result& res);
void to_json(nlohmann::json& j, const std::vector<Admin>& roles);

std::string to_string(const Admin& r);

}
