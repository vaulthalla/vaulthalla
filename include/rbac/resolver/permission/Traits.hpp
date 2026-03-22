#pragma once

#include <type_traits>
#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    template<typename Enum>
    struct PermissionTargetTraits {
    };

    template<typename Enum>
    struct PermissionContextPolicyTraits {
    };

    template<typename RoleT>
    struct PermissionResolverEnumPack;

    template<typename Traits, typename RoleT>
    concept HasMutableTarget = requires(RoleT &role)
    {
        Traits::target(role);
    };

    template<typename Traits, typename RoleT>
    concept HasConstTarget = requires(const RoleT &role)
    {
        Traits::target(role);
    };

    template<typename Traits, typename RoleT>
    concept HasMutableResolve = requires(RoleT &role, const std::vector<std::string_view> &parts)
    {
        Traits::resolve(role, parts);
    };

    template<typename Traits, typename RoleT>
    concept HasConstResolve = requires(const RoleT &role, const std::vector<std::string_view> &parts)
    {
        Traits::resolve(role, parts);
    };
}
