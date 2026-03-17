#pragma once

#include "rbac/permission/template/ModuleSet.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace vault {
        enum class RolePermissions : uint8_t {
            None = 0,
            Assign = 1 << 0,
            Modify = 1 << 1,
            Revoke = 1 << 2,
            All = Assign | Modify | Revoke
        };
    }

    template<>
    struct PermissionTraits<vault::RolePermissions> {
        using Entry = PermissionEntry<vault::RolePermissions>;

        static constexpr std::array entries{
            Entry{vault::RolePermissions::Assign, "assign", "Allows user to assign roles in a vault to others."},
            Entry{vault::RolePermissions::Modify, "modify", "Allows user to modify roles in a vault, including changing permissions and renaming."},
            Entry{vault::RolePermissions::Revoke, "revoke", "Allows user to revoke roles and consequently access in vault."},
        };
    };

    namespace vault {
        struct Roles final : ModuleSet<uint8_t, RolePermissions, uint8_t> {
            static constexpr const auto* FLAG_CONTEXT = "roles";

            Roles() = default;
            explicit Roles(const Mask& mask) : ModuleSet(mask) {}

            [[nodiscard]] const char* name() const override { return FLAG_CONTEXT; }
            [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
            [[nodiscard]] std::string toString(uint8_t indent) const override;
            [[nodiscard]] std::string toFlagsString() const override { return joinFlagsWithOwn(); }

            [[nodiscard]] Mask toMask() const override { return packWithOwn(); }
            void fromMask(const Mask mask) override { unpackWithOwn(mask); }

            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportWithOwn("vault.roles");
            }

            [[nodiscard]] bool canAssign() const noexcept { return has(RolePermissions::Assign); }
            [[nodiscard]] bool canModify() const noexcept { return has(RolePermissions::Modify); }
            [[nodiscard]] bool canRevoke() const noexcept { return has(RolePermissions::Revoke); }

            static Roles None() {
                Roles r;
                r.clear();
                return r;
            }

            static Roles Assigner() {
                Roles r;
                r.clear();
                r.grant(RolePermissions::Assign);
                return r;
            }

            static Roles Editor() {
                Roles r;
                r.clear();
                r.grant(RolePermissions::Modify);
                return r;
            }

            static Roles Moderator() {
                Roles r;
                r.clear();
                r.grant(RolePermissions::Assign);
                r.grant(RolePermissions::Revoke);
                return r;
            }

            static Roles Manager() {
                Roles r;
                r.clear();
                r.grant(RolePermissions::Assign);
                r.grant(RolePermissions::Modify);
                return r;
            }

            static Roles Full() {
                Roles r;
                r.clear();
                r.grant(RolePermissions::All);
                return r;
            }

            static Roles Custom(const SetMask mask) {
                Roles r;
                r.setRawFromSetMask(mask);
                return r;
            }
        };

        void to_json(nlohmann::json& j, const Roles& r);
        void from_json(const nlohmann::json& j, Roles& r);
    }
}
