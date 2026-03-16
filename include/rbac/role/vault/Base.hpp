#pragma once

#include "rbac/permission/vault/Roles.hpp"
#include "rbac/permission/vault/Sync.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }

namespace vh::rbac::role::vault {

struct Base {
    permission::vault::Roles roles{};
    permission::vault::Sync sync{};
    permission::vault::Filesystem fs{};

    virtual ~Base() = default;
    Base() = default;
    explicit Base(const pqxx::row& row);
    Base(const pqxx::row& row, const pqxx::result& overrides);

    [[nodiscard]] virtual std::string toString(uint8_t indent) const;
    [[nodiscard]] std::string toFlagsString() const;
};

void to_json(nlohmann::json& j, const Base& v);
void from_json(const nlohmann::json& j, Base& v);

}
