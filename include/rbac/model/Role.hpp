#pragma once

#include "Permission.hpp"

#include <string>
#include <ctime>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }

namespace vh::rbac::model {

struct Role {
    unsigned int id{};
    std::string name, description, type;
    std::time_t created_at{};
    uint16_t permissions{0};

    Role() = default;
    explicit Role(const pqxx::row& row);
    explicit Role(const nlohmann::json& j);
    Role(std::string name, std::string description, std::string type, uint16_t permissions);

    virtual ~Role() = default;

    virtual std::string permissions_to_flags_string() const;

    static std::string underscore_to_hyphens(const std::string& s);
};

void to_json(nlohmann::json& j, const Role& r);
void from_json(const nlohmann::json& j, Role& r);
void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Role>>& roles);

std::vector<std::shared_ptr<Role>> roles_from_pq_res(const pqxx::result& res);

std::string to_string(const std::shared_ptr<Role>& r);
std::string to_string(const std::vector<std::shared_ptr<Role>>& roles);

}
