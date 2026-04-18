#pragma once

#include <type_traits>
#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    enum class RoleDomain { Admin, Vault };

    template<typename Enum>
    struct PermissionTargetTraits {
        static constexpr bool canOverride = false;
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

    template<typename T>
    struct is_shared_ptr : std::false_type {};

    template<typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    template<typename T>
    inline constexpr bool is_shared_ptr_v = is_shared_ptr<std::remove_cvref_t<T>>::value;
}
