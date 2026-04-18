#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/admin/roles/Admin.hpp"
#include "rbac/permission/admin/roles/Vault.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

    struct Roles final : Module<uint16_t> {
        static constexpr const auto* ModuleName = "roles";

        roles::Admin admin;
        roles::Vault vault;

        Roles() = default;
        explicit Roles(const Mask mask) { fromMask(mask); }

        [[nodiscard]] const char *name() const override { return ModuleName; }

        [[nodiscard]] std::vector<std::string> getFlags() const override { return Module::getFlags(admin, vault); } ;

        [[nodiscard]] Mask toMask() const override { return pack(admin, vault); }
        void fromMask(const Mask mask) override { unpack(mask, admin, vault); }

        [[nodiscard]] std::string toFlagsString() const override;
        [[nodiscard]] std::string toString(uint8_t indent) const override;

        [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
            return packAndExportPerms(
                mount("admin.roles.admin", admin),
                mount("admin.roles.vault", vault)
            );
        }

        static Roles None() {
            Roles r;
            r.admin = roles::Admin::None();
            r.vault = roles::Vault::None();
            return r;
        }

        static Roles ViewOnly() {
            Roles r;
            r.admin = roles::Admin::View();
            r.vault = roles::Vault::View();
            return r;
        }

        static Roles AdminManager() {
            Roles r;
            r.admin = roles::Admin::Manager();
            r.vault = roles::Vault::View();
            return r;
        }

        static Roles VaultManager() {
            Roles r;
            r.admin = roles::Admin::View();
            r.vault = roles::Vault::Manager();
            return r;
        }

        static Roles LifecycleManager() {
            Roles r;
            r.admin = roles::Admin::Lifecycle();
            r.vault = roles::Vault::Lifecycle();
            return r;
        }

        static Roles Full() {
            Roles r;
            r.admin = roles::Admin::Full();
            r.vault = roles::Vault::Full();
            return r;
        }

        static Roles Custom(roles::Admin admin, roles::Vault vault) {
            Roles r;
            r.admin = std::move(admin);
            r.vault = std::move(vault);
            return r;
        }
    };

    void to_json(nlohmann::json& j, const Roles& r);
    void from_json(const nlohmann::json& j, Roles& r);

}
