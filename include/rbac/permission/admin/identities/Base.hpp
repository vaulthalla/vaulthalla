#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

enum class IdentityPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Add = 1 << 1,
    Edit = 1 << 2,
    Delete = 1 << 3,
    All = View | Add | Edit | Delete
};

struct Base : Set<IdentityPermissions, uint8_t> {

    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(IdentityPermissions::View); }
    [[nodiscard]] bool canAdd() const noexcept { return has(IdentityPermissions::Add); }
    [[nodiscard]] bool canEdit() const noexcept { return has(IdentityPermissions::Edit); }
    [[nodiscard]] bool canDelete() const noexcept { return has(IdentityPermissions::Delete); }
};

void to_json(nlohmann::json& j, const Base& p);
void from_json(const nlohmann::json& j, Base& p);

}
