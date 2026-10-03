#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // admin.roles.{admin,vault}.<permission>
    template<>
    struct PermissionContextPolicyTraits<permission::admin::roles::RolesPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT, typename Base = std::conditional_t<std::is_const_v<RoleT>,
            const permission::admin::roles::Base, permission::admin::roles::Base>>
        static Base* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4 || parts[0] != "admin" || parts[1] != "roles") return nullptr;
            if (parts[2] == "admin") return &role.roles.admin;
            if (parts[2] == "vault") return &role.roles.vault;
            return nullptr;
        }
    };
}
