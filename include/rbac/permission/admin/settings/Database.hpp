#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Database final : Base {
    static constexpr const auto* FLAG_CONTEXT = "db";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Database None() {
        Database d;
        d.clear();
        return d;
    }

    static Database View() {
        Database d;
        d.clear();
        d.grant(SettingsPermissions::View);
        return d;
    }

    static Database Edit() {
        auto d = View();
        d.grant(SettingsPermissions::Edit);
        return d;
    }
};

void to_json(nlohmann::json& j, const Database& s);
void from_json(const nlohmann::json& j, Database& s);

}
