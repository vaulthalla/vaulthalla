#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Http final : Base {
    static constexpr const auto* FLAG_CONTEXT = "http";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Http None() {
        Http h;
        h.clear();
        return h;
    }

    static Http View() {
        Http h;
        h.clear();
        h.grant(SettingsPermissions::View);
        return h;
    }

    static Http Edit() {
        auto h = View();
        h.grant(SettingsPermissions::Edit);
        return h;
    }
};

void to_json(nlohmann::json& j, const Http& s);
void from_json(const nlohmann::json& j, Http& s);

}
