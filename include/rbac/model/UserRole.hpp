#pragma once

#include "Permission.hpp"
#include "rbac/model/Role.hpp"

#include <memory>
#include <string>
#include <ctime>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::model {

struct UserRole final : Role {
    unsigned int assignment_id{}, user_id{};
    std::time_t assigned_at{};

    UserRole() = default;
    ~UserRole() override = default;
    explicit UserRole(const pqxx::row& row);
    explicit UserRole(const nlohmann::json& j);

    explicit UserRole(const Role& r) : Role(r) {
        if (type != "user") throw std::runtime_error("UserRole: invalid role type");
    }

    std::string permissions_to_flags_string() const override;

    static std::shared_ptr<UserRole> fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const UserRole& r);
void from_json(const nlohmann::json& j, UserRole& r);

std::vector<std::shared_ptr<UserRole>> user_roles_from_pq_res(const pqxx::result& res);
void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<UserRole>>& roles);

std::string to_string(const std::shared_ptr<UserRole>& role);

}
