#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Services final : Base {
    static constexpr const auto* FLAG_CONTEXT = "services";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Services None() {
        Services s;
        s.clear();
        return s;
    }

    static Services View() {
        Services s;
        s.clear();
        s.grant(SettingsPermissions::View);
        return s;
    }

    static Services Edit() {
        auto s = View();
        s.grant(SettingsPermissions::Edit);
        return s;
    }
};

void to_json(nlohmann::json& j, const Services& s);
void from_json(const nlohmann::json& j, Services& s);

}
