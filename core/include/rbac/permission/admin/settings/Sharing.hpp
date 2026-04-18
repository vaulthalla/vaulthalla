#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Sharing final : Base {
    static constexpr const auto* FLAG_CONTEXT = "settings-share";
    static constexpr const auto* DESCRIPTION_OBJECT = "sharing";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_OBJECT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Sharing None() {
        Sharing s;
        s.clear();
        return s;
    }

    static Sharing View() {
        Sharing s;
        s.clear();
        s.grant(SettingsPermissions::View);
        return s;
    }

    static Sharing Edit() {
        Sharing s;
        s.clear();
        s.grant(SettingsPermissions::All);
        return s;
    }
};

void to_json(nlohmann::json& j, const Sharing& s);
void from_json(const nlohmann::json& j, Sharing& s);

}
