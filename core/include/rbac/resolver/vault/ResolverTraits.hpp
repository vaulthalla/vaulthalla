#pragma once

#include "rbac/resolver/ResolverTraits/Fwd.hpp"
#include "rbac/role/vault/Base.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/permission/admin/VaultGlobals.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"

#include <utility>

namespace vh::rbac::resolver {
    template<>
    struct VaultResolverTraits<permission::vault::fs::FilePermissions> {
        static const auto &direct(const role::Vault &role) { return role.files(); }

        static const auto &self(const decltype(std::declval<identities::User>().vaultGlobals().self) &perms) {
            return perms.files();
        }

        static const auto &admin(const decltype(std::declval<identities::User>().vaultGlobals().admin) &perms) {
            return perms.files();
        }

        static const auto &user(const decltype(std::declval<identities::User>().vaultGlobals().user) &perms) {
            return perms.files();
        }
    };

    template<>
    struct VaultResolverTraits<permission::vault::fs::DirectoryPermissions> {
        static const auto &direct(const role::Vault &role) { return role.directories(); }

        static const auto &self(const decltype(std::declval<identities::User>().vaultGlobals().self) &perms) {
            return perms.directories();
        }

        static const auto &admin(const decltype(std::declval<identities::User>().vaultGlobals().admin) &perms) {
            return perms.directories();
        }

        static const auto &user(const decltype(std::declval<identities::User>().vaultGlobals().user) &perms) {
            return perms.directories();
        }
    };

    template<>
    struct VaultResolverTraits<permission::vault::RolePermissions> {
        static const auto &direct(const role::Vault &role) { return role.rolesPerms(); }

        static const auto &self(const decltype(std::declval<identities::User>().vaultGlobals().self) &perms) {
            return perms.rolesPerms();
        }

        static const auto &admin(const decltype(std::declval<identities::User>().vaultGlobals().admin) &perms) {
            return perms.rolesPerms();
        }

        static const auto &user(const decltype(std::declval<identities::User>().vaultGlobals().user) &perms) {
            return perms.rolesPerms();
        }
    };

    template<>
    struct VaultResolverTraits<permission::vault::sync::SyncConfigPermissions> {
        static const auto &direct(const role::Vault &role) { return role.syncConfig(); }

        static const auto &self(const decltype(std::declval<identities::User>().vaultGlobals().self) &perms) {
            return perms.syncConfig();
        }

        static const auto &admin(const decltype(std::declval<identities::User>().vaultGlobals().admin) &perms) {
            return perms.syncConfig();
        }

        static const auto &user(const decltype(std::declval<identities::User>().vaultGlobals().user) &perms) {
            return perms.syncConfig();
        }
    };

    template<>
    struct VaultResolverTraits<permission::vault::sync::SyncActionPermissions> {
        static const auto &direct(const role::Vault &role) { return role.syncActions(); }

        static const auto &self(const decltype(std::declval<identities::User>().vaultGlobals().self) &perms) {
            return perms.syncActions();
        }

        static const auto &admin(const decltype(std::declval<identities::User>().vaultGlobals().admin) &perms) {
            return perms.syncActions();
        }

        static const auto &user(const decltype(std::declval<identities::User>().vaultGlobals().user) &perms) {
            return perms.syncActions();
        }
    };
}
