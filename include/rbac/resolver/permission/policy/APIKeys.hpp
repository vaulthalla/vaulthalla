#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionContextPolicyTraits<permission::admin::keys::APIPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 5) return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
            if (parts[1] != "keys") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
            if (parts[2] != "api") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);

            if (parts[3] == "admin") return &role.keys.apiKeys.admin;
            if (parts[3] == "user")  return &role.keys.apiKeys.user;
            if (parts[3] == "self") return &role.keys.apiKeys.self;

            return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
        }

        template<typename RoleT>
        static const auto* resolve(const RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 5) return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);

            if (parts[0] != "admin") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
            if (parts[1] != "keys") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
            if (parts[2] != "api") return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);

            if (parts[3] == "admin") return &role.keys.apiKeys.admin;
            if (parts[3] == "user")  return &role.keys.apiKeys.user;
            if (parts[3] == "self") return &role.keys.apiKeys.self;

            return static_cast<decltype(&role.keys.apiKeys.admin)>(nullptr);
        }
    };
}
