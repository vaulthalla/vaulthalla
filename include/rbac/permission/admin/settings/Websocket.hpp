#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Websocket final : Base {
    static constexpr const auto* FLAG_CONTEXT = "ws";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Websocket None() {
        Websocket w;
        w.clear();
        return w;
    }

    static Websocket View() {
        Websocket w;
        w.clear();
        w.grant(SettingsPermissions::View);
        return w;
    }

    static Websocket Edit() {
        auto w = View();
        w.grant(SettingsPermissions::Edit);
        return w;
    }
};

void to_json(nlohmann::json& j, const Websocket& s);
void from_json(const nlohmann::json& j, Websocket& s);

}
