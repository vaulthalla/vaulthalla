#pragma once

#include "Base.hpp"
#include "rbac/permission/Admin.hpp"

#include <memory>
#include <string>
#include <ctime>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::role {

struct Admin final : Base {
    unsigned int user_id{};
    permission::Admin permissions{};

    Admin() = default;
    ~Admin() override = default;
    Admin(const pqxx::row& row, const pqxx::result& globalVaultRoles);
    explicit Admin(const nlohmann::json& j);

    explicit Admin(const Base& r) : Base(r) {
        if (type != "user") throw std::runtime_error("UserRole: invalid role type");
    }

    [[nodiscard]] std::string permissions_to_flags_string() const override;

    static std::shared_ptr<Admin> fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Admin& r);
void from_json(const nlohmann::json& j, Admin& r);

std::vector<std::shared_ptr<Admin>> user_roles_from_pq_res(const pqxx::result& res);
void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Admin>>& roles);

std::string to_string(const std::shared_ptr<Admin>& role);

}
