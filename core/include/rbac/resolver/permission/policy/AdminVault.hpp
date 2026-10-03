#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // admin.vaults.{self,admin,user}.<permission>
    template<>
    struct PermissionContextPolicyTraits<permission::admin::VaultPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            using Ptr = decltype(&role.vaults.admin);
            if (parts.size() < 4 || parts[0] != "admin" || parts[1] != "vaults") return static_cast<Ptr>(nullptr);
            if (parts[2] == "admin") return &role.vaults.admin;
            if (parts[2] == "user") return &role.vaults.user;
            if (parts[2] == "self") return &role.vaults.self;
            return static_cast<Ptr>(nullptr);
        }
    };
}
