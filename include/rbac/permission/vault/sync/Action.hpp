#pragma once

#include "rbac/permission/template/Set.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault::sync {

enum class SyncActionPermissions : uint8_t {
    None = 0,
    Trigger = 1 << 0,
};

struct Action final : Set<SyncActionPermissions, uint8_t> {
    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] bool canTrigger() const noexcept { return has(SyncActionPermissions::Trigger); }
    [[nodiscard]] bool none() const noexcept { return has(SyncActionPermissions::None); }
};

void to_json(nlohmann::json& j, const Action& a);
void from_json(const nlohmann::json& j, Action& a);

}