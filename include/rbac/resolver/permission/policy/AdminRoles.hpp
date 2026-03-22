#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionContextPolicyTraits<permission::admin::roles::RolesPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.roles.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.roles.admin)>(nullptr);
            if (parts[1] != "roles") return static_cast<decltype(&role.roles.admin)>(nullptr);

            if (parts[2] == "admin") return &role.roles.admin;
            if (parts[2] == "vault")  return &role.roles.vault;

            return static_cast<decltype(&role.roles.admin)>(nullptr);
        }

        template<typename RoleT>
        static const auto* resolve(const RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.roles.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.roles.admin)>(nullptr);
            if (parts[1] != "roles") return static_cast<decltype(&role.roles.admin)>(nullptr);

            if (parts[2] == "admin") return &role.roles.admin;
            if (parts[2] == "vault")  return &role.roles.vault;

            return static_cast<decltype(&role.roles.admin)>(nullptr);
        }
    };
}
