#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/permission/admin/all.hpp"
#include "rbac/permission/Permission.hpp"

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::role {
    struct Admin final : Meta {
        std::optional<uint32_t> user_id;

        permission::admin::Identities identities{};
        permission::admin::Vaults vaults{};
        permission::admin::Audits audits{};
        permission::admin::Settings settings{};
        permission::admin::Roles roles{};
        permission::admin::Keys keys{};
        permission::admin::VaultGlobals vGlobals{};

        Admin() = default;

        Admin(const pqxx::row &row, const pqxx::result &globalVaultRoles);

        explicit Admin(const pqxx::row &row);

        explicit Admin(const nlohmann::json &j);

        [[nodiscard]] std::string toString(uint8_t indent) const override;

        [[nodiscard]] std::string toString() const { return toString(0); }

        [[nodiscard]] std::string toFlagsString() const;

        [[nodiscard]] std::vector<std::string> getFlags() const;

        [[nodiscard]] std::vector<permission::Permission> toPermissions() const;

        [[nodiscard]] std::unordered_map<std::string, permission::Permission> toFlagMap() const;

        [[nodiscard]] static std::string usage();

        static Admin fromJson(const nlohmann::json &j);

        // ---------- canonical builtins ----------

        static Admin None(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "unprivileged",
                "User with no administrative privileges.",
                userId,
                permission::admin::Identities::None(),
                permission::admin::Vaults::None(),
                permission::admin::Audits::None(),
                permission::admin::Settings::None(),
                permission::admin::Roles::None(),
                permission::admin::Keys::None(),
                permission::admin::VaultGlobals::NoneIfBound(userId)
            );
        }

        static Admin Auditor(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "auditor",
                "Read-only administrative role for inspection, auditing, and operational visibility.",
                userId,
                permission::admin::Identities::ViewOnly(),
                permission::admin::Vaults::ViewOnly(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::ViewOnly(),
                permission::admin::Roles::ViewOnly(),
                permission::admin::Keys::ViewOnly(),
                permission::admin::VaultGlobals::ReaderIfBound(userId)
            );
        }

        static Admin Support(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "support",
                "Support role for user assistance, troubleshooting, and limited administrative visibility.",
                userId,
                permission::admin::Identities::UserSupport(),
                permission::admin::Vaults::ViewOnly(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::Support(),
                permission::admin::Roles::ViewOnly(),
                permission::admin::Keys::ViewOnly(),
                permission::admin::VaultGlobals::ReaderIfBound(userId)
            );
        }

        static Admin IdentityAdmin(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "identity_admin",
                "Administrative role focused on user, group, and identity lifecycle management.",
                userId,
                permission::admin::Identities::UserAndGroupManager(),
                permission::admin::Vaults::ViewOnly(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::ViewOnly(),
                permission::admin::Roles::LifecycleManager(),
                permission::admin::Keys::ViewOnly(),
                permission::admin::VaultGlobals::ManagerIfBound(userId)
            );
        }

        static Admin SecurityAdmin(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "security_admin",
                "Security-focused administrative role with strong visibility into audits, auth settings, and sensitive key material.",
                userId,
                permission::admin::Identities::PrivilegedIdentityManager(),
                permission::admin::Vaults::ViewOnly(),
                permission::admin::Audits::Full(),
                permission::admin::Settings::SecurityAdmin(),
                permission::admin::Roles::AdminManager(),
                permission::admin::Keys::SecurityAdmin(),
                permission::admin::VaultGlobals::ManagerIfBound(userId)
            );
        }

        static Admin PlatformOperator(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "platform_operator",
                "Operational administrative role for infrastructure, service control, sync, and platform key management.",
                userId,
                permission::admin::Identities::UserManager(),
                permission::admin::Vaults::UserManager(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::OperationsAdmin(),
                permission::admin::Roles::VaultManager(),
                permission::admin::Keys::PlatformOperator(),
                permission::admin::VaultGlobals::PowerUserIfBound(userId)
            );
        }

        static Admin VaultAdmin(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "vault_admin",
                "Administrative role focused on vault governance, lifecycle, and broad vault-scoped control.",
                userId,
                permission::admin::Identities::UserAndGroupManager(),
                permission::admin::Vaults::Full(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::ViewOnly(),
                permission::admin::Roles::VaultManager(),
                permission::admin::Keys::APIKeyManager(),
                permission::admin::VaultGlobals::PowerUserIfBound(userId)
            );
        }

        static Admin OrgAdmin(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "admin",
                "System administrator with broad organizational control across identities, vaults, settings, roles, and keys.",
                userId,
                permission::admin::Identities::PrivilegedIdentityManager(),
                permission::admin::Vaults::Full(),
                permission::admin::Audits::Full(),
                permission::admin::Settings::Full(),
                permission::admin::Roles::Full(),
                permission::admin::Keys::SecurityAdmin(),
                permission::admin::VaultGlobals::FullIfBound(userId)
            );
        }

        static Admin SuperAdmin(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "super_admin",
                "Root-level system owner with unrestricted administrative authority.",
                userId,
                permission::admin::Identities::Full(),
                permission::admin::Vaults::Full(),
                permission::admin::Audits::Full(),
                permission::admin::Settings::Full(),
                permission::admin::Roles::Full(),
                permission::admin::Keys::Full(),
                permission::admin::VaultGlobals::FullIfBound(userId)
            );
        }

        static Admin KeyCustodian(const std::optional<uint32_t> userId = std::nullopt) {
            return make(
                "key_custodian",
                "Highly restricted administrative role trusted with sensitive encryption key custody and related key operations.",
                userId,
                permission::admin::Identities::ViewOnly(),
                permission::admin::Vaults::ViewOnly(),
                permission::admin::Audits::ViewOnly(),
                permission::admin::Settings::SecurityAuditor(),
                permission::admin::Roles::ViewOnly(),
                permission::admin::Keys::KeyCustodian(),
                permission::admin::VaultGlobals::ReaderIfBound(userId)
            );
        }

        static Admin Custom(
            std::string name,
            std::string description,
            permission::admin::Identities identities,
            permission::admin::Vaults vaults,
            permission::admin::Audits audits,
            permission::admin::Settings settings,
            permission::admin::Roles roles,
            permission::admin::Keys keys,
            const std::optional<uint32_t> userId = std::nullopt,
            std::optional<permission::admin::VaultGlobals> vGlobals = std::nullopt
        ) {
            return make(
                std::move(name),
                std::move(description),
                userId,
                std::move(identities),
                std::move(vaults),
                std::move(audits),
                std::move(settings),
                std::move(roles),
                std::move(keys),
                std::move(vGlobals).value_or(permission::admin::VaultGlobals::NoneIfBound(userId))
            );
        }

    private:
        static Admin make(
            std::string name,
            std::string description,
            const std::optional<uint32_t> userId,
            permission::admin::Identities identities,
            permission::admin::Vaults vaults,
            permission::admin::Audits audits,
            permission::admin::Settings settings,
            permission::admin::Roles roles,
            permission::admin::Keys keys,
            permission::admin::VaultGlobals vGlobals
        ) {
            Admin a;
            a.name = std::move(name);
            a.description = std::move(description);
            a.user_id = userId;

            a.identities = std::move(identities);
            a.vaults = std::move(vaults);
            a.audits = std::move(audits);
            a.settings = std::move(settings);
            a.roles = std::move(roles);
            a.keys = std::move(keys);
            a.vGlobals = std::move(vGlobals);

            return a;
        }
    };

    void to_json(nlohmann::json &j, const Admin &a);

    void from_json(const nlohmann::json &j, Admin &a);

    std::vector<Admin> admin_roles_from_pq_res(const pqxx::result &res);

    void to_json(nlohmann::json &j, const std::vector<Admin> &roles);

    void to_json(nlohmann::json &j, const std::vector<std::shared_ptr<Admin> > &roles);

    std::string to_string(const Admin &r);

    std::string to_string(const std::vector<std::shared_ptr<Admin>> &roles);
}
