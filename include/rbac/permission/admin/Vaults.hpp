#pragma once

#include "rbac/permission/Vault.hpp"

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

}
