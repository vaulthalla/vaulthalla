#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <nlohmann/json_fwd.hpp>
#include <utility>

namespace pqxx { class result; }

namespace vh::rbac::permission {

    namespace admin {
        enum class VaultPermissions : uint8_t {
            None      = 0,
            View      = 1 << 0,
            ViewStats = 1 << 1,
            Create    = 1 << 2,
            Edit      = 1 << 3,
            Remove    = 1 << 4,
            All       = View | ViewStats | Create | Edit | Remove
        };
    }

    template <>
    struct PermissionTraits<admin::VaultPermissions> {
        using Entry = PermissionEntry<admin::VaultPermissions>;

        static constexpr std::array entries {
            Entry{admin::VaultPermissions::View, "view", "Allows viewing vault details for '{}'"},
            Entry{admin::VaultPermissions::ViewStats, "view_stats", "Allows viewing vault statistics for '{}'"},
            Entry{admin::VaultPermissions::Create, "create", "Allows creating vaults for '{}'"},
            Entry{admin::VaultPermissions::Edit, "edit", "Allows editing vault details for '{}'"},
            Entry{admin::VaultPermissions::Remove, "remove", "Allows vault deletion for '{}'"},
        };
    };

    namespace admin {
        struct Vault : Set<VaultPermissions, uint8_t> {
            std::string flag_prefix;
            std::string descriptionContext{"unset"};

            explicit Vault(std::string module, std::string entity)
                : flag_prefix(std::move(module) + "-" + entity),
                  descriptionContext(std::move(entity)) {}

            [[nodiscard]] const char* flagPrefix() const override { return flag_prefix.c_str(); }
            [[nodiscard]] std::string_view descriptionObject() const { return descriptionContext; }
            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(VaultPermissions::View); }
            [[nodiscard]] bool canViewStats() const noexcept { return has(VaultPermissions::ViewStats); }
            [[nodiscard]] bool canCreate() const noexcept { return has(VaultPermissions::Create); }
            [[nodiscard]] bool canEdit() const noexcept { return has(VaultPermissions::Edit); }
            [[nodiscard]] bool canRemove() const noexcept { return has(VaultPermissions::Remove); }

            static Vault None(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                return v;
            }

            static Vault View(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::View);
                return v;
            }

            static Vault ViewWithStats(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::View);
                v.grant(VaultPermissions::ViewStats);
                return v;
            }

            static Vault Create(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::View);
                v.grant(VaultPermissions::ViewStats);
                v.grant(VaultPermissions::Create);
                return v;
            }

            static Vault Edit(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::View);
                v.grant(VaultPermissions::ViewStats);
                v.grant(VaultPermissions::Edit);
                return v;
            }

            static Vault Manage(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::View);
                v.grant(VaultPermissions::ViewStats);
                v.grant(VaultPermissions::Create);
                v.grant(VaultPermissions::Edit);
                return v;
            }

            static Vault Full(std::string module, std::string entity) {
                Vault v{std::move(module), std::move(entity)};
                v.clear();
                v.grant(VaultPermissions::All);
                return v;
            }
        };

        struct Vaults : Module<uint32_t> {
            static constexpr const auto* MODULE_NAME = "vault";

            enum class Type : uint8_t { Self, Admin, User };

            Vault self{std::string(MODULE_NAME), "self"};
            Vault admin{std::string(MODULE_NAME), "admin"};
            Vault user{std::string(MODULE_NAME), "user"};

            Vaults() = default;
            explicit Vaults(const Mask mask) { fromMask(mask); }

            [[nodiscard]] const char* name() const override { return MODULE_NAME; }
            [[nodiscard]] std::string toString(uint8_t indent) const override;
            [[nodiscard]] std::string toFlagsString() const override;
            [[nodiscard]] Mask toMask() const override { return pack(self, admin, user); }
            void fromMask(const Mask mask) override { unpack(mask, self, admin, user); }

            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportPerms(
                    mount("admin.vaults.self", self),
                    mount("admin.vaults.admin", admin),
                    mount("admin.vaults.user", user)
                );
            }

            static Vaults None() {
                Vaults v;
                v.self = Vault::None(MODULE_NAME, "self");
                v.admin = Vault::None(MODULE_NAME, "admin");
                v.user = Vault::None(MODULE_NAME, "user");
                return v;
            }

            static Vaults ViewOnly() {
                Vaults v;
                v.self = Vault::ViewWithStats(MODULE_NAME, "self");
                v.admin = Vault::ViewWithStats(MODULE_NAME, "admin");
                v.user = Vault::ViewWithStats(MODULE_NAME, "user");
                return v;
            }

            static Vaults SelfService() {
                Vaults v;
                v.self = Vault::Manage(MODULE_NAME, "self");
                v.admin = Vault::None(MODULE_NAME, "admin");
                v.user = Vault::None(MODULE_NAME, "user");
                return v;
            }

            static Vaults UserManager() {
                Vaults v;
                v.self = Vault::ViewWithStats(MODULE_NAME, "self");
                v.admin = Vault::None(MODULE_NAME, "admin");
                v.user = Vault::Manage(MODULE_NAME, "user");
                return v;
            }

            static Vaults AdminManager() {
                Vaults v;
                v.self = Vault::ViewWithStats(MODULE_NAME, "self");
                v.admin = Vault::Manage(MODULE_NAME, "admin");
                v.user = Vault::ViewWithStats(MODULE_NAME, "user");
                return v;
            }

            static Vaults Operator() {
                Vaults v;
                v.self = Vault::Manage(MODULE_NAME, "self");
                v.admin = Vault::Edit(MODULE_NAME, "admin");
                v.user = Vault::Manage(MODULE_NAME, "user");
                return v;
            }

            static Vaults Provisioner() {
                Vaults v;
                v.self = Vault::Create(MODULE_NAME, "self");
                v.admin = Vault::Create(MODULE_NAME, "admin");
                v.user = Vault::Create(MODULE_NAME, "user");
                return v;
            }

            static Vaults Full() {
                Vaults v;
                v.self = Vault::Full(MODULE_NAME, "self");
                v.admin = Vault::Full(MODULE_NAME, "admin");
                v.user = Vault::Full(MODULE_NAME, "user");
                return v;
            }

            static Vaults Custom(Vault self, Vault admin, Vault user) {
                Vaults v;
                v.self = std::move(self);
                v.admin = std::move(admin);
                v.user = std::move(user);
                return v;
            }
        };

        void to_json(nlohmann::json& j, const Vault& v);
        void from_json(const nlohmann::json& j, Vault& v);

        void to_json(nlohmann::json& j, const Vaults& v);
        void from_json(const nlohmann::json& j, Vaults& v);

        std::string to_string(const Vaults::Type& type);
        Vaults::Type vault_type_from_string(const std::string& str);
    }

}
