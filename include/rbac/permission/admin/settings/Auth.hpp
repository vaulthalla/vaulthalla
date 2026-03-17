#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Auth final : Base {
    static constexpr const auto* FLAG_CONTEXT = "auth";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Auth None() {
        Auth a;
        a.clear();
        return a;
    }
    static Auth View() {
        Auth a;
        a.clear();
        a.grant(SettingsPermissions::View);
        return a;
    }

    static Auth Edit() {
        auto a = View();
        a.grant(SettingsPermissions::Edit);
        return a;
    }
};

void to_json(nlohmann::json& j, const Auth& s);
void from_json(const nlohmann::json& j, Auth& s);

}
