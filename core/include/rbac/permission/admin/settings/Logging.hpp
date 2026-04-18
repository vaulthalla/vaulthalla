#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Logging final : Base {
    static constexpr const auto* FLAG_CONTEXT = "settings-log";
    static constexpr const auto* DESCRIPTION_OBJECT = "logging";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_OBJECT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Logging None() {
        Logging l;
        l.clear();
        return l;
    }

    static Logging View() {
        Logging l;
        l.clear();
        l.grant(SettingsPermissions::View);
        return l;
    }

    static Logging Edit() {
        Logging l;
        l.clear();
        l.grant(SettingsPermissions::All);
        return l;
    }
};

void to_json(nlohmann::json& j, const Logging& s);
void from_json(const nlohmann::json& j, Logging& s);

}
