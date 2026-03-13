#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

enum class SettingsPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Edit = 1 << 1,
    All = View | Edit
};

struct Base : Set<SettingsPermissions, uint8_t> {
    [[nodiscard]] bool canView() const noexcept { return has(SettingsPermissions::View); }
    [[nodiscard]] bool canEdit() const noexcept { return has(SettingsPermissions::Edit); }

    void operator=(const Base& other) = default;
};

void to_json(nlohmann::json& j, const Base& s);
void from_json(const nlohmann::json& j, Base& s);

}
