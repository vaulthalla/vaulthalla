#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace admin::identities {
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
    }

    template<>
    struct PermissionTraits<admin::identities::GroupPermissions> {
        using Entry = PermissionEntry<admin::identities::GroupPermissions>;

        static constexpr std::array entries{
            Entry{admin::identities::GroupPermissions::View, "view", "Allows viewing or listing of groups"},
            Entry{admin::identities::GroupPermissions::Add, "add", "Allows creating new groups"},
            Entry{admin::identities::GroupPermissions::Edit, "edit", "Allows editing of groups"},
            Entry{admin::identities::GroupPermissions::Delete, "delete", "Allows deleting groups"},
            Entry{admin::identities::GroupPermissions::AddMember, "add-member", "Allows adding members"},
            Entry{admin::identities::GroupPermissions::RemoveMember, "remove-member", "Allows removing members"},
            Entry{
                admin::identities::GroupPermissions::ViewMembers,
                "view-members",
                "Allows viewing or listing group members"
            }
        };
    };

    namespace admin::identities {
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

            [[nodiscard]] bool canManageMembers() const noexcept {
                return canAddMember() || canRemoveMember();
            }

            static Groups None() {
                Groups g;
                g.clear();
                return g;
            }

            static Groups ViewOnly() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                return g;
            }

            static Groups Creator() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::Add);
                return g;
            }

            static Groups Editor() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::Edit);
                return g;
            }

            static Groups Deleter() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::Delete);
                return g;
            }

            static Groups MemberViewer() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::ViewMembers);
                return g;
            }

            static Groups MemberManager() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::ViewMembers);
                g.grant(GroupPermissions::AddMember);
                g.grant(GroupPermissions::RemoveMember);
                return g;
            }

            static Groups GroupManager() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::Add);
                g.grant(GroupPermissions::Edit);
                g.grant(GroupPermissions::ViewMembers);
                return g;
            }

            static Groups Lifecycle() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::View);
                g.grant(GroupPermissions::Add);
                g.grant(GroupPermissions::Delete);
                g.grant(GroupPermissions::ViewMembers);
                return g;
            }

            static Groups Full() {
                Groups g;
                g.clear();
                g.grant(GroupPermissions::All);
                return g;
            }

            static Groups Custom(const Mask mask) {
                Groups g;
                g.setRaw(mask);
                return g;
            }
        };

        void to_json(nlohmann::json& j, const Groups& o);
        void from_json(const nlohmann::json& j, Groups& o);
    }
}
