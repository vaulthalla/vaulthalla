#pragma once

#include "Traits.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionTargetTraits<permission::admin::AuditPermissions> {
        static constexpr auto domain = RoleDomain::Admin;
        static constexpr bool canOverride = false;

        static auto& target(role::Admin& role) { return role.audits; }
        static const auto& target(const role::Admin& role) { return role.audits; }
    };

    template<>
    struct PermissionTargetTraits<permission::admin::keys::EncryptionKeyPermissions> {
        static constexpr auto domain = RoleDomain::Admin;
        static constexpr bool canOverride = false;

        static auto& target(role::Admin& role) { return role.keys.encryptionKeys; }
        static const auto& target(const role::Admin& role) { return role.keys.encryptionKeys; }
    };

    template<>
    struct PermissionTargetTraits<permission::vault::RolePermissions> {
        static constexpr auto domain = RoleDomain::Vault;
        static constexpr bool canOverride = false;

        static auto& target(role::Vault& role) { return role.roles; }
        static const auto& target(const role::Vault& role) { return role.roles; }
    };

    template<>
    struct PermissionTargetTraits<permission::vault::sync::SyncConfigPermissions> {
        static constexpr auto domain = RoleDomain::Vault;
        static constexpr bool canOverride = false;

        static auto& target(role::Vault& role) { return role.sync.config; }
        static const auto& target(const role::Vault& role) { return role.sync.config; }
    };

    template<>
    struct PermissionTargetTraits<permission::vault::sync::SyncActionPermissions> {
        static constexpr auto domain = RoleDomain::Vault;
        static constexpr bool canOverride = false;

        static auto& target(role::Vault& role) { return role.sync.action; }
        static const auto& target(const role::Vault& role) { return role.sync.action; }
    };

    template<>
    struct PermissionTargetTraits<permission::vault::fs::FilePermissions> {
        static constexpr auto domain = RoleDomain::Vault;
        static constexpr bool canOverride = true;

        static auto& target(role::Vault& role) { return role.fs.files; }
        static const auto& target(const role::Vault& role) { return role.fs.files; }
    };

    template<>
    struct PermissionTargetTraits<permission::vault::fs::DirectoryPermissions> {
        static constexpr auto domain = RoleDomain::Vault;
        static constexpr bool canOverride = true;

        static auto& target(role::Vault& role) { return role.fs.directories; }
        static const auto& target(const role::Vault& role) { return role.fs.directories; }
    };
}
