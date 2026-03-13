#pragma once

#include "rbac/permission/vault/Keys.hpp"
#include "rbac/permission/vault/Roles.hpp"
#include "rbac/permission/vault/Sync.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::rbac::permission {

struct Vault {
    vault::Keys keys{};
    vault::Roles roles{};
    vault::Sync sync{};
    vault::Filesystem filesystem{};

    Vault() = default;
    explicit Vault(const pqxx::row& row);

    [[nodiscard]] std::string toString(uint8_t indent) const;
};

void to_json(nlohmann::json& j, const Vault& v);
void from_json(const nlohmann::json& j, Vault& v);

}
