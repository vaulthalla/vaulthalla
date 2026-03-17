#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <bitset>
#include <nlohmann/json_fwd.hpp>
#include <pqxx/row>

namespace vh::rbac::permission {

struct Permission {
    uint32_t id{}, bit_position{};
    std::string name, description;
    std::time_t created_at{}, updated_at{};

    Permission() = default;
    explicit Permission(const pqxx::row& row);
    explicit Permission(const nlohmann::json& j);
    Permission(uint32_t bitPos, std::string name, std::string description);
};
    
void to_json(nlohmann::json& j, const Permission& p);
void from_json(const nlohmann::json& j, Permission& p);

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Permission>>& permissions);

}
