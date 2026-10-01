#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // admin.identities.{users,admins}.<permission>. Groups have their own enum (GroupPermissions) and target trait.
    template<>
    struct PermissionContextPolicyTraits<permission::admin::identities::IdentityPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT, typename Base = std::conditional_t<std::is_const_v<RoleT>,
            const permission::admin::identities::Base, permission::admin::identities::Base>>
        static Base* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4 || parts[0] != "admin" || parts[1] != "identities") return nullptr;
            if (parts[2] == "admins") return &role.identities.admins;
            if (parts[2] == "users") return &role.identities.users;
            return nullptr;
        }
    };
}
