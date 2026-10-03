#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // admin.keys.api.{self,admin,user}.<permission>
    template<>
    struct PermissionContextPolicyTraits<permission::admin::keys::APIPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            using Ptr = decltype(&role.keys.apiKeys.admin);
            if (parts.size() < 5 || parts[0] != "admin" || parts[1] != "keys" || parts[2] != "api")
                return static_cast<Ptr>(nullptr);
            if (parts[3] == "admin") return &role.keys.apiKeys.admin;
            if (parts[3] == "user") return &role.keys.apiKeys.user;
            if (parts[3] == "self") return &role.keys.apiKeys.self;
            return static_cast<Ptr>(nullptr);
        }
    };
}
