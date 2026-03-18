#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {

    namespace admin::settings {
        enum class SettingsPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            Edit = 1 << 1,
            All = View | Edit
        };
    }

    template <>
    struct PermissionTraits<admin::settings::SettingsPermissions> {
        using Entry = PermissionEntry<admin::settings::SettingsPermissions>;

        static constexpr std::array entries {
            Entry{admin::settings::SettingsPermissions::View, "view", "Allows viewing settings in module: {}"},
            Entry{admin::settings::SettingsPermissions::Edit, "edit", "Allows editing settings in module: {}"},
        };
    };

    namespace admin::settings {
        struct Base : Set<SettingsPermissions, uint8_t> {
            [[nodiscard]] const char* flagPrefix() const override = 0;
            [[nodiscard]] std::string toString(uint8_t indent) const override;
            [[nodiscard]] virtual std::string_view descriptionObject() const = 0;

            [[nodiscard]] bool canView() const noexcept { return has(SettingsPermissions::View); }
            [[nodiscard]] bool canEdit() const noexcept { return has(SettingsPermissions::Edit); }
        };

        void to_json(nlohmann::json& j, const Base& s);
        void from_json(const nlohmann::json& j, Base& s);
    }

}
