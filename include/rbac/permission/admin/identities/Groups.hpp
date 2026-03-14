#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

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
    ViewMembers = 1 << 6,
    All = View | Add | Edit | Delete | AddMember | RemoveMember | ViewMembers
};

struct Groups final : Set<GroupPermissions, uint8_t> {
    static constexpr const auto* FLAG_CONTEXT = "groups";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(GroupPermissions::View); }
    [[nodiscard]] bool canAdd() const noexcept { return has(GroupPermissions::Add); }
    [[nodiscard]] bool canEdit() const noexcept { return has(GroupPermissions::Edit); }
    [[nodiscard]] bool canDelete() const noexcept { return has(GroupPermissions::Delete); }
    [[nodiscard]] bool canAddMember() const noexcept { return has(GroupPermissions::AddMember); }
    [[nodiscard]] bool canRemoveMember() const noexcept { return has(GroupPermissions::RemoveMember); }
    [[nodiscard]] bool canViewMembers() const noexcept { return has(GroupPermissions::ViewMembers); }
};

void to_json(nlohmann::json& j, const Groups& o);
void from_json(const nlohmann::json& j, Groups& o);

}

template <>
struct vh::rbac::permission::PermissionTraits<vh::rbac::permission::admin::identities::GroupPermissions> {
    using E = PermissionEntry<admin::identities::GroupPermissions>;

    static constexpr std::array entries {
        E{admin::identities::GroupPermissions::View, "view"},
        E{admin::identities::GroupPermissions::Add, "add"},
        E{admin::identities::GroupPermissions::Edit, "edit"},
        E{admin::identities::GroupPermissions::Delete, "delete"},
        E{admin::identities::GroupPermissions::AddMember, "add-member"},
        E{admin::identities::GroupPermissions::RemoveMember, "remove-member"},
        E{admin::identities::GroupPermissions::ViewMembers, "view-members"}
    };
};
