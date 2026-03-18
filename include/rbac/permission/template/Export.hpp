#pragma once

#include "rbac/permission/Permission.hpp"

#include <concepts>
#include <cstddef>
#include <string_view>
#include <vector>

namespace vh::rbac::permission {
    template<typename MaskT>
    struct PackedPermissionExportT {
        MaskT mask{};
        std::vector<Permission> permissions;

        [[nodiscard]] const std::vector<Permission> &operator()() const & { return permissions; }
        [[nodiscard]] std::vector<Permission> &operator()() & { return permissions; }
        [[nodiscard]] std::vector<Permission> &&operator()() && { return std::move(permissions); }

        [[nodiscard]] std::size_t size() const { return permissions.size(); }
    };

    template<typename T>
    concept SetLike =
            requires(const T &t)
            {
                { T::bitWidth() } -> std::convertible_to<std::size_t>;
                { t.raw() };
            };

    template<typename T>
    concept ExportableSetLike =
            SetLike<T> &&
            requires(const T &t, std::vector<Permission> &out, std::string_view qualifiedName)
            {
                t.exportPermissions(t, std::back_inserter(out), std::size_t{}, qualifiedName);
            };

    template<typename SetT>
    struct MountedSetRef {
        std::string_view qualifiedName;
        const SetT &set;
    };

    template<typename SetT>
    [[nodiscard]] MountedSetRef<SetT> mount(std::string_view qualifiedName, const SetT &set) {
        return {qualifiedName, set};
    }
}
