#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

enum class RolePermissions : uint8_t {
    None = 0,
    Assign = 1 << 0,
    Modify = 1 << 1,
    Revoke = 1 << 2,
    All = Assign | Modify | Revoke
};

struct Roles : Set<RolePermissions, uint8_t> {
    Roles() = default;
    explicit Roles(const Mask& mask) : Set(mask) {}

    [[nodiscard]] bool canAssign() const noexcept { return has(RolePermissions::Assign); }
    [[nodiscard]] bool canModify() const noexcept { return has(RolePermissions::Modify); }
    [[nodiscard]] bool canRevoke() const noexcept { return has(RolePermissions::Revoke); }
    [[nodiscard]] bool any() const noexcept { return has(RolePermissions::All); }
    [[nodiscard]] bool none() const noexcept { return has(RolePermissions::None); }
};

void to_json(nlohmann::json& j, const Roles& r);
void from_json(const nlohmann::json& j, Roles& r);

}
