#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/permission/Vault.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <regex>
#include <memory>
#include <optional>
#include <unordered_map>

namespace vh::rbac::role {

struct Vault final : Meta {
    struct AssignmentInfo {
        uint32_t subject_id{}, vault_id{};
        std::string subject_type; // 'user' or 'group'

        [[nodiscard]] std::string toString(uint8_t indent) const;
    };

    std::optional<AssignmentInfo> assignment;  // Can exist as a template or assigned role
    permission::Vault permissions{};

    Vault() = default;
    Vault(const pqxx::row& row, const pqxx::result& overrides);
    explicit Vault(const nlohmann::json& j);

    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] std::string toString() const { return toString(0); }

    static Vault fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Vault& r);
void from_json(const nlohmann::json& j, Vault& r);

void to_json(nlohmann::json& j, const std::vector<Vault>& roles);
void to_json(nlohmann::json& j, const std::unordered_map<uint32_t, std::shared_ptr<Vault>>& roles);

void to_json(nlohmann::json& j, const Vault::AssignmentInfo& r);
void from_json(const nlohmann::json& j, Vault::AssignmentInfo& r);

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Vault>>& roles);

std::vector<Vault> vault_roles_from_json(const nlohmann::json& j);
std::unordered_map<uint32_t, std::shared_ptr<Vault>> vault_roles_from_pq_result(const pqxx::result& res, const pqxx::result& overrides);

std::string to_string(const Vault& role);
std::string to_string(const std::vector<Vault>& roles);

}