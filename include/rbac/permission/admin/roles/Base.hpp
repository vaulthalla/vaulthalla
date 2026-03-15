#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::roles {

    enum class RolesPermissions : uint8_t {
        None = 0,
        View = 1 << 0,
        Add = 1 << 1,
        Edit = 1 << 2,
        Delete = 1 << 3,
        All = View | Add | Edit | Delete
    };

    struct Base : Set<RolesPermissions, uint8_t> {
        [[nodiscard]] const char* flagPrefix() const override = 0;
        [[nodiscard]] std::string toString(uint8_t indent) const override;

        [[nodiscard]] bool canView() const noexcept { return has(RolesPermissions::View); }
        [[nodiscard]] bool canAdd() const noexcept { return has(RolesPermissions::Add); }
        [[nodiscard]] bool canEdit() const noexcept { return has(RolesPermissions::Edit); }
        [[nodiscard]] bool canDelete() const noexcept { return has(RolesPermissions::Delete); }
    };

    void to_json(nlohmann::json& j, const Base& p);
    void from_json(const nlohmann::json& j, Base& p);

}

template<>
struct vh::rbac::permission::PermissionTraits<vh::rbac::permission::admin::roles::RolesPermissions> {
    using E = PermissionEntry<admin::roles::RolesPermissions>;

    static constexpr std::array entries {
        E{ admin::roles::RolesPermissions::View, "view" },
        E{ admin::roles::RolesPermissions::Add, "add" },
        E{ admin::roles::RolesPermissions::Edit, "edit" },
        E{ admin::roles::RolesPermissions::Delete, "delete" }
    };
};
