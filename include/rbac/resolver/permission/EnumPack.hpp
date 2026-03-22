#pragma once

#include "Traits.hpp"
#include "rbac/resolver/Permission.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

namespace vh::rbac::resolver {
    template<>
    struct PermissionResolverEnumPack<role::Admin> {
        using type = PermissionResolver<
            role::Admin,
            permission::admin::keys::APIPermissions,
            permission::admin::keys::EncryptionKeyPermissions,
            permission::admin::VaultPermissions,
            permission::admin::identities::IdentityPermissions,
            permission::admin::settings::SettingsPermissions,
            permission::admin::AuditPermissions,
            permission::admin::roles::RolesPermissions
        >;
    };

    template<>
    struct PermissionResolverEnumPack<role::Vault> {
        using type = PermissionResolver<
            role::Vault,
        permission::vault::RolePermissions,
        permission::vault::sync::SyncActionPermissions,
        permission::vault::sync::SyncConfigPermissions,
        permission::vault::fs::FilePermissions,
        permission::vault::fs::DirectoryPermissions
        >;
    };
}
