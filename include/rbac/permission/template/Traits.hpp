#pragma once

#include <array>
#include <string_view>
#include <type_traits>

namespace vh::rbac::permission {
    template<typename EnumT>
    struct PermissionEntry {
        EnumT value;
        std::string_view slug;
        std::string_view description;
    };

    template<typename EnumT>
    struct PermissionTraits;

    template<typename EnumT>
    concept HasPermissionTraits =
            std::is_enum_v<EnumT> &&
            requires
            {
                PermissionTraits<EnumT>::entries;
            };
}
