// Every exported role permission must be applicable through the resolver that both the CLI (--allow-*/--deny-*)
// and the web (role.*.add/update snapshots) use. A missing or mis-typed trait used to make apply() a silent no-op
// for 77 of 87 admin permissions and every share/vault-global permission, which broke role editing on both surfaces.

#include "rbac/resolver/permission/EnumPack.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/role/vault/Global.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>

namespace vh::test_role_permissions {

using rbac::resolver::PermissionOperation;

template<class RoleT>
void expectEveryPermissionRoundTrips(std::shared_ptr<RoleT> role, const char* label) {
    using Resolver = typename rbac::resolver::PermissionResolverEnumPack<std::shared_ptr<RoleT>>::type;
    const auto perms = role->toPermissions();
    ASSERT_FALSE(perms.empty()) << label;

    std::set<std::string> names;
    for (const auto& p : perms) {
        EXPECT_TRUE(names.insert(p.qualified_name).second) << label << ": duplicate " << p.qualified_name;
        EXPECT_TRUE(Resolver::apply(role, p, PermissionOperation::Grant)) << label << ": grant " << p.qualified_name;
        EXPECT_TRUE(Resolver::has(role, p)) << label << ": not granted " << p.qualified_name;
    }
    for (const auto& p : perms) {
        EXPECT_TRUE(Resolver::apply(role, p, PermissionOperation::Revoke)) << label << ": revoke " << p.qualified_name;
        EXPECT_FALSE(Resolver::has(role, p)) << label << ": still granted " << p.qualified_name;
    }
}

TEST(RolePermissions, EveryAdminPermissionGrantsAndRevokes) {
    expectEveryPermissionRoundTrips(std::make_shared<rbac::role::Admin>(rbac::role::Admin::None()), "admin");
}

TEST(RolePermissions, EveryVaultPermissionGrantsAndRevokes) {
    expectEveryPermissionRoundTrips(std::make_shared<rbac::role::Vault>(), "vault");
}

TEST(RolePermissions, EveryVaultGlobalPermissionGrantsAndRevokes) {
    expectEveryPermissionRoundTrips(std::make_shared<rbac::role::vault::Global>(), "vault-global");
}

// The CLI accepts exactly the flags `vh permission` documents (getFlags()); each must map to one permission, and the
// flag map must be collision-free (buildFlagMap throws on a duplicate).
template<class RoleT>
void expectDocumentedFlagsResolve(std::shared_ptr<RoleT> role, const char* label) {
    using Resolver = typename rbac::resolver::PermissionResolverEnumPack<std::shared_ptr<RoleT>>::type;
    const auto byFlag = Resolver::buildFlagMap(role->toPermissions());
    const auto documented = role->getFlags();
    ASSERT_FALSE(documented.empty()) << label;
    for (const auto& flag : documented)
        EXPECT_TRUE(byFlag.contains(flag)) << label << ": documented flag " << flag << " does not resolve";
}

TEST(RolePermissions, DocumentedCliFlagsResolveToPermissions) {
    expectDocumentedFlagsResolve(std::make_shared<rbac::role::Admin>(rbac::role::Admin::None()), "admin");
    expectDocumentedFlagsResolve(std::make_shared<rbac::role::Vault>(), "vault");
}

TEST(RolePermissions, GrantingEverySuperAdminPermissionReproducesSuperAdmin) {
    using Resolver = rbac::resolver::PermissionResolverEnumPack<std::shared_ptr<rbac::role::Admin>>::type;
    const auto super = std::make_shared<rbac::role::Admin>(rbac::role::Admin::SuperAdmin());
    auto staged = std::make_shared<rbac::role::Admin>(rbac::role::Admin::None());
    for (const auto& p : super->toPermissions())
        if (Resolver::has(super, p)) ASSERT_TRUE(Resolver::apply(staged, p, PermissionOperation::Grant)) << p.qualified_name;

    EXPECT_EQ(staged->identities.toBitString(), super->identities.toBitString());
    EXPECT_EQ(staged->vaults.toBitString(), super->vaults.toBitString());
    EXPECT_EQ(staged->audits.toBitString(), super->audits.toBitString());
    EXPECT_EQ(staged->settings.toBitString(), super->settings.toBitString());
    EXPECT_EQ(staged->roles.toBitString(), super->roles.toBitString());
    EXPECT_EQ(staged->keys.toBitString(), super->keys.toBitString());
    EXPECT_EQ(staged->s3Gateway.toBitString(), super->s3Gateway.toBitString());
}

}
