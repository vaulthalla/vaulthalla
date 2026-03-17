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

    static Base None() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::None();
        b.fs = permission::vault::Filesystem::None();
        return b;
    }

    static Base BrowseOnly() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::None();
        b.fs = permission::vault::Filesystem::BrowseOnly();
        return b;
    }

    static Base Reader() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::Viewer();
        b.fs = permission::vault::Filesystem::ReadOnly();
        return b;
    }

    static Base Contributor() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::Operator();
        b.fs = permission::vault::Filesystem::Contributor();
        return b;
    }

    static Base Editor() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::Operator();
        b.fs = permission::vault::Filesystem::Editor();
        return b;
    }

    static Base Manager() {
        Base b;
        b.roles = permission::vault::Roles::Moderator();
        b.sync = permission::vault::Sync::Manager();
        b.fs = permission::vault::Filesystem::Manager();
        return b;
    }

    static Base SyncOperator() {
        Base b;
        b.roles = permission::vault::Roles::None();
        b.sync = permission::vault::Sync::Manager();
        b.fs = permission::vault::Filesystem::ReadOnly();
        return b;
    }

    static Base RoleManager() {
        Base b;
        b.roles = permission::vault::Roles::Full();
        b.sync = permission::vault::Sync::Viewer();
        b.fs = permission::vault::Filesystem::ReadOnly();
        return b;
    }

    static Base PowerUser() {
        Base b;
        b.roles = permission::vault::Roles::Moderator();
        b.sync = permission::vault::Sync::Manager();
        b.fs = permission::vault::Filesystem::Full();
        return b;
    }

    static Base Full() {
        Base b;
        b.roles = permission::vault::Roles::Full();
        b.sync = permission::vault::Sync::Full();
        b.fs = permission::vault::Filesystem::Full();
        return b;
    }

    static Base Custom(
        permission::vault::Roles roles,
        permission::vault::Sync sync,
        permission::vault::Filesystem fs
    ) {
        Base b;
        b.roles = std::move(roles);
        b.sync = std::move(sync);
        b.fs = std::move(fs);
        return b;
    }
};

void to_json(nlohmann::json& j, const Base& v);
void from_json(const nlohmann::json& j, Base& v);

}
