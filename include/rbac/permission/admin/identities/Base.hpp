#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {

    namespace admin::identities {
        enum class IdentityPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            Add = 1 << 1,
            Edit = 1 << 2,
            Delete = 1 << 3,
            All = View | Add | Edit | Delete
        };
    }

    template<>
    struct PermissionTraits<admin::identities::IdentityPermissions> {
        using Entry = PermissionEntry<admin::identities::IdentityPermissions>;
        using P = admin::identities::IdentityPermissions;

        static constexpr std::array entries {
            Entry{ P::View, "view", "Allows viewing or listing of {}s"},
            Entry{ P::Add, "add", "Allows adding new {}s"},
            Entry{ P::Edit, "edit", "Allows editing existing {}s"},
            Entry{ P::Delete, "delete", "Allows deleting existing {}s"},
        };
    };

    namespace admin::identities {
        struct Base : Set<IdentityPermissions, uint8_t> {
            [[nodiscard]] const char* flagPrefix() const override = 0;
            [[nodiscard]] virtual std::string_view descriptionObject() const = 0;
            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(IdentityPermissions::View); }
            [[nodiscard]] bool canAdd() const noexcept { return has(IdentityPermissions::Add); }
            [[nodiscard]] bool canEdit() const noexcept { return has(IdentityPermissions::Edit); }
            [[nodiscard]] bool canDelete() const noexcept { return has(IdentityPermissions::Delete); }
        };

        void to_json(nlohmann::json& j, const Base& p);
        void from_json(const nlohmann::json& j, Base& p);
    }

}
