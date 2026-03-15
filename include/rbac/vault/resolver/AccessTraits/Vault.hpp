#pragma once

#include "Fwd.hpp"

#include "rbac/permission/Vault.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"

#include <utility>

namespace vh::rbac::vault {

    template<>
    struct AccessTraits<permission::vault::fs::FilePermissions> {
        static const auto& direct(const role::Vault& role) { return role.files(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.files(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.files(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.files(); }
    };

    template<>
    struct AccessTraits<permission::vault::fs::DirectoryPermissions> {
        static const auto& direct(const role::Vault& role) { return role.directories(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.directories(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.directories(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.directories(); }
    };

    template<>
    struct AccessTraits<permission::vault::APIKeyPermissions> {
        static const auto& direct(const role::Vault& role) { return role.apiKey(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.apiKey(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.apiKey(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.apiKey(); }
    };

    template<>
    struct AccessTraits<permission::vault::EncryptionKeyPermissions> {
        static const auto& direct(const role::Vault& role) { return role.encryptionKey(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.encryptionKey(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.encryptionKey(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.encryptionKey(); }
    };

    template<>
    struct AccessTraits<permission::vault::RolePermissions> {
        static const auto& direct(const role::Vault& role) { return role.rolesPerms(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.rolesPerms(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.rolesPerms(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.rolesPerms(); }
    };

    template<>
    struct AccessTraits<permission::vault::sync::SyncConfigPermissions> {
        static const auto& direct(const role::Vault& role) { return role.syncConfig(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.syncConfig(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.syncConfig(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.syncConfig(); }
    };

    template<>
    struct AccessTraits<permission::vault::sync::SyncActionPermissions> {
        static const auto& direct(const role::Vault& role) { return role.syncActions(); }
        static const auto& self(const decltype(std::declval<identities::User>().globalVaultPerms().self)& perms) { return perms.syncActions(); }
        static const auto& admin(const decltype(std::declval<identities::User>().globalVaultPerms().admin)& perms) { return perms.syncActions(); }
        static const auto& user(const decltype(std::declval<identities::User>().globalVaultPerms().user)& perms) { return perms.syncActions(); }
    };

}
