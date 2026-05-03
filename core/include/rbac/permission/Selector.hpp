#pragma once

#include "rbac/permission/Permission.hpp"

#include <optional>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace vh::rbac::permission {

template<typename EnumT>
[[nodiscard]] std::optional<Permission> findPermissionForEnumValue(
    const std::vector<Permission>& permissions,
    const EnumT value
) {
    static_assert(std::is_enum_v<EnumT>, "findPermissionForEnumValue requires an enum type");

    const auto expectedType = std::type_index(typeid(EnumT));
    const auto expectedRaw = static_cast<uint64_t>(value);
    for (const auto& permission : permissions)
        if (permission.enumType == expectedType && permission.rawValue == expectedRaw)
            return permission;

    return std::nullopt;
}

template<typename EnumT>
[[nodiscard]] Permission requirePermissionForEnumValue(
    const std::vector<Permission>& permissions,
    const EnumT value
) {
    if (auto permission = findPermissionForEnumValue(permissions, value)) return *permission;
    throw std::invalid_argument("Permission set does not contain requested enum value");
}

}
