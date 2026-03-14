#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/permission/Vault.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <regex>
#include <optional>
#include <unordered_map>

namespace vh::rbac::role::vault {

struct Override;

struct Global final : BasicMeta {
    enum class Scope { Self, User, Admin };

    uint32_t user_id{};
    Scope scope{Scope::Self};
    std::optional<uint32_t> template_role_id;
    bool enforce_template{false};
    permission::Vault permissions{};

    Global() = default;

    explicit Global(const pqxx::row& row);
    explicit Global(const nlohmann::json& j);

    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] std::string toString() const { return toString(0); }

    static Global fromJson(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Global& r);
void from_json(const nlohmann::json& j, Global& r);

std::vector<Global> global_vault_roles_from_json(const nlohmann::json& j);
std::vector<Global> global_vault_roles_from_pq_result(const pqxx::result& res);

std::string to_string(const Global& role);

std::string to_string(const Global::Scope& scope);
Global::Scope global_vault_role_scope_from_string(const std::string& name);

}
