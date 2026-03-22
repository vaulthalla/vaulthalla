#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionContextPolicyTraits<permission::admin::VaultPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.vaults.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.vaults.admin)>(nullptr);
            if (parts[1] != "roles") return static_cast<decltype(&role.vaults.admin)>(nullptr);

            if (parts[2] == "admin") return &role.vaults.admin;
            if (parts[2] == "user")  return &role.vaults.user;
            if (parts[2] == "self") return &role.vaults.self;

            return static_cast<decltype(&role.vaults.admin)>(nullptr);
        }

        template<typename RoleT>
        static const auto* resolve(const RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.vaults.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.vaults.admin)>(nullptr);
            if (parts[1] != "roles") return static_cast<decltype(&role.vaults.admin)>(nullptr);

            if (parts[2] == "admin") return &role.vaults.admin;
            if (parts[2] == "user")  return &role.vaults.user;
            if (parts[2] == "self") return &role.vaults.self;

            return static_cast<decltype(&role.vaults.admin)>(nullptr);
        }
    };
}
