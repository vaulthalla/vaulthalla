#pragma once

#include "rbac/permission/template/Set.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

enum class GroupPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Add = 1 << 1,
    Edit = 1 << 2,
    Delete = 1 << 3,
    AddMember = 1 << 4,
    RemoveMember = 1 << 5,
    All = View | Add | Edit | Delete | AddMember | RemoveMember
};

struct Groups final : Set<GroupPermissions, uint8_t> {
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(GroupPermissions::View); }
    [[nodiscard]] bool canAdd() const noexcept { return has(GroupPermissions::Add); }
    [[nodiscard]] bool canEdit() const noexcept { return has(GroupPermissions::Edit); }
    [[nodiscard]] bool canDelete() const noexcept { return has(GroupPermissions::Delete); }
    [[nodiscard]] bool canAddMember() const noexcept { return has(GroupPermissions::AddMember); }
    [[nodiscard]] bool canRemoveMember() const noexcept { return has(GroupPermissions::RemoveMember); }
};

void to_json(nlohmann::json& j, const Groups& o);
void from_json(const nlohmann::json& j, Groups& o);

}
