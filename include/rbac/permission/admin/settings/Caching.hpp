#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Caching final : Base {
    static constexpr const auto* FLAG_CONTEXT = "caching";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Caching None() {
        Caching c;
        c.clear();
        return c;
    }

    static Caching View() {
        Caching c;
        c.clear();
        c.grant(SettingsPermissions::View);
        return c;
    }

    static Caching Edit() {
        auto c = View();
        c.grant(SettingsPermissions::Edit);
        return c;
    }
};

void to_json(nlohmann::json& j, const Caching& s);
void from_json(const nlohmann::json& j, Caching& s);

}
