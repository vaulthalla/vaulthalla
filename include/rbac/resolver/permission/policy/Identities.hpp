#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionContextPolicyTraits<permission::admin::identities::IdentityPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            // expected qualifiedName shape examples:
            // "identities.admins.assign"
            // "identities.users.modify"
            // "identities.groups.view"

            if (parts.size() < 4) return static_cast<decltype(&role.identities.admins)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.identities.admins)>(nullptr);
            if (parts[1] != "identities") return static_cast<decltype(&role.identities.admins)>(nullptr);

            if (parts[2] == "admins") return &role.identities.admins;
            if (parts[2] == "users")  return &role.identities.users;
            if (parts[2] == "groups") return &role.identities.groups;

            return static_cast<decltype(&role.identities.admins)>(nullptr);
        }

        template<typename RoleT>
        static const auto* resolve(const RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.identities.admins)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.identities.admins)>(nullptr);
            if (parts[1] != "identities") return static_cast<decltype(&role.identities.admins)>(nullptr);

            if (parts[2] == "admins") return &role.identities.admins;
            if (parts[2] == "users")  return &role.identities.users;
            if (parts[2] == "groups") return &role.identities.groups;

            return static_cast<decltype(&role.identities.admins)>(nullptr);
        }
    };
}
