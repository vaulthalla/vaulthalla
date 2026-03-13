#pragma once

#include "rbac/permission/template/Set.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault::sync {

enum class SyncConfigPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Edit = 1 << 1,
    All = View | Edit,
};

struct Config final : Set<SyncConfigPermissions, uint8_t> {
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(SyncConfigPermissions::View); }
    [[nodiscard]] bool canEdit() const noexcept { return has(SyncConfigPermissions::Edit); }
    [[nodiscard]] bool all() const noexcept { return has(SyncConfigPermissions::All); }
    [[nodiscard]] bool none() const noexcept { return has(SyncConfigPermissions::None); }
};

void to_json(nlohmann::json& j, const Config& cfg);
void from_json(const nlohmann::json& j, Config& cfg);

}
