#pragma once

#include "rbac/permission/Vault.hpp"

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class result; }

namespace vh::rbac::permission::admin {

struct Vaults {
    enum class Type { Self, Admin, User };

    Vault self{}, admin{}, user{};

    Vaults() = default;
    explicit Vaults(const pqxx::result& res);
};

std::string to_string(const Vaults::Type& type);
Vaults::Type vault_type_from_string(const std::string& str);

void to_json(nlohmann::json& j, const Vaults& v);
void from_json(const nlohmann::json& j, Vaults& v);

}
