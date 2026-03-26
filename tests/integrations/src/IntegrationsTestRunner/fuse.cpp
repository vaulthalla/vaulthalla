#include "IntegrationsTestRunner.hpp"
#include "fuse/helpers.hpp"
#include "fuse/Builder.hpp"
#include "identities/User.hpp"

#include <array>

#include "rbac/role/Vault.hpp"

using namespace vh::test::integration::fuse;
using namespace vh::rbac;
using namespace vh::identities;

namespace vh::test::integration {

    static TestStage testFUSECRUD() {
        auto builder = Builder::make({
            .name = "CRUD",
            .baseDir = "crud_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeTestCase({
            .name = "FUSE mkdir (admin)",
            .path = "fuse/mkdir",
            .must_contain = {"OK mkdir"},
            .fn = [=]{ return mkdir_as(*ctx.admin->meta.linux_uid, ctx.base()); }
        });

        builder.makeTestCase({
            .name = "FUSE write (admin)",
            .path = "fuse/write",
            .must_contain = {"OK write"},
            .fn = [=]{ return write_as(*ctx.admin->meta.linux_uid, ctx.hello(), "hello world!\n"); }
        });

        builder.makeTestCase({
            .name = "FUSE read (admin)",
            .path = "fuse/read",
            .fn = [=] { return read_as(*ctx.admin->meta.linux_uid, ctx.hello()); }
        });

        builder.makeTestCase({
            .name = "FUSE rename (admin)",
            .path = "fuse/rename",
            .must_contain = {"OK mv"},
            .fn = [=]{ return mv_as(*ctx.admin->meta.linux_uid, ctx.hello(), ctx.base() / "hello2.txt"); }
        });

        builder.makeTestCase({
            .name = "FUSE rm -rf (admin)",
            .path = "fuse/rmrf",
            .must_contain = {"OK rm -rf"},
            .fn = [=]{ return rmrf_as(*ctx.admin->meta.linux_uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testFUSEAllow() {
        auto builder = Builder::make({
            .name = "Permissions Allow",
            .baseDir = "perm_allow_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/allow" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/allow",
            .description = "Vault role with permissions to test allow cases",
        });

        builder.makeTestCase({
             .name = "FUSE allow: ls seed",
             .path = "fuse/ls",
             .fn = [=]{ return ls_as(subj.uid, ctx.base()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: read secret",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: write user_note",
            .path = "fuse/write",
            .must_contain = {"OK write"},
            .fn = [=]{ return write_as(subj.uid, ctx.note(), "hey\n"); },
        });

        return builder.exec();
    }

    static TestStage testFUSEDeny() {
        auto builder = Builder::make({
            .name = "Permissions Deny",
            .baseDir = "perm_deny_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/deny" });

        builder.makeTestCase({
            .name = "FUSE deny: ls seed",
            .path = "fuse/ls",
            .expect_exit = EACCES,
            .fn = [=]{ return ls_as(subj.uid, ctx.base()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: read secret",
            .path = "fuse/read",
            .expect_exit = EACCES,
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: write hax",
            .path = "fuse/write",
            .expect_exit = EACCES,
            .fn = [=]{ return write_as(subj.uid, ctx.docs() / "hax.txt", "nope\n"); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: rm -rf seed",
            .path = "fuse/rmrf",
            .expect_exit = EACCES,
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testVaultPermOverridesAllow() {
        auto builder = Builder::make({
            .name = "Vault Permission Overrides Allow",
            .baseDir = "perm_override_allow_seed"
        });

        auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/override" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::ImplicitDeny().name,
            .roleNameSeed = "vault_role/create/override",
            .description = "Vault role with override",
        });

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::ALLOW,
            .pattern = ctx.docs() / "*.txt"
        });

        builder.makeTestCase({
            .name = "FUSE override allow: read secret",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: read note",
            .path = "fuse/read",
            .expect_exit = EACCES,
            .fn = [=]{ return read_as(subj.uid, ctx.note()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: rm -rf seed",
            .path = "fuse/rmrf",
            .expect_exit = EACCES,
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testVaultPermOverridesDeny() {
        auto builder = Builder::make({
            .name = "Vault Permission Overrides Deny",
            .baseDir = "perm_override_deny_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/override_deny" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/override_deny",
            .description = "Vault role with override",
        });

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::DENY,
            .pattern = ctx.base() / "docs" / "*.txt"
        });

        builder.makeTestCase({
            .name = "FUSE override deny: read secret",
            .path = "fuse/read",
            .expect_exit = EACCES,
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: read note",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.note()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: rm -rf seed",
            .path = "fuse/rmrf",
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testFUSEGroupPermissions() {
        auto builder = Builder::make({
            .name = "Group Permissions",
            .baseDir = "group_perm_allow_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/group_perm" });
        builder.makeGroup("group/create/group_perm");
        builder.addUserToGroup();

        builder.buildAssignVRole({
            .subjectType = TargetSubject::Group,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/group_perm",
            .description = "Vault role for testing group perms",
        });

        builder.makeTestCase({
            .name = "FUSE allow: ls seed",
            .path = "fuse/ls",
            .fn = [=]{ return ls_as(subj.uid, ctx.base()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: read secret",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: write user_note",
            .path = "fuse/write",
            .must_contain = {"OK write"},
            .fn = [=]{ return write_as(subj.uid, ctx.note(), "hey\n"); }
        });

        return builder.exec();
    }

    static TestStage testGroupPermOverrides() {
        auto builder = Builder::make({
            .name = "Group Permission Overrides",
            .baseDir = "group_perm_override_deny_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/group_override" });
        builder.makeGroup("group/create/group_override");
        builder.addUserToGroup();

        builder.buildAssignVRole({
            .subjectType = TargetSubject::Group,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/group_override",
            .description = "Vault role for testing group override perms",
        });

        builder.makeOverride({
            .subjectType = TargetSubject::Group,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::DENY,
            .pattern = ctx.docs() / "*.txt"
        });

        builder.makeTestCase({
            .name = "FUSE override deny: read secret",
            .path = "fuse/read",
            .expect_exit = EACCES,
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: read note",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.note()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: rm -rf seed",
            .path = "fuse/rmrf",
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testFUSEUserOverridesGroupOverride() {
        auto builder = Builder::make({
            .name = "User Vault Perm Override - Overrides Group Vault Perm Override",
            .baseDir = "user_override_group_override_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeUser({ .userNameSeed = "user/create/override_deny" });
        builder.makeGroup("group/create/override_deny");
        builder.addUserToGroup();

        builder.buildAssignVRole({
            .subjectType = TargetSubject::Group,
            .templateName = role::Vault::ImplicitDeny().name,
            .roleNameSeed = "vault_role/create/override_deny_group",
            .description = "Vault role for testing group override perms",
        });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::ImplicitDeny().name,
            .roleNameSeed = "vault_role/create/override_deny_user",
            .description = "Vault role for testing user override perms",
        });

        const auto pattern = ctx.docs() / "*.txt";

        builder.makeOverride({
            .subjectType = TargetSubject::Group,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::DENY,
            .pattern = pattern
        });

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::ALLOW,
            .pattern = pattern
        });

        builder.makeTestCase({
            .name = "FUSE override user allow/group deny: read secret",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: read note",
            .path = "fuse/read",
            .fn = [=]{ return read_as(subj.uid, ctx.note()); }
        });

        builder.makeTestCase({
            .name = "FUSE allow: rm -rf seed",
            .path = "fuse/rmrf",
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    void IntegrationsTestRunner::runFUSETests() {
        constexpr std::array always_run {
            testFUSECRUD
        };

        for (const auto& function : always_run) {
            auto stage = function();
            validateStage(stage);

            for (const auto& uid : stage.uids) linux_uids_.push_back(uid);
            for (const auto& gid : stage.gids) linux_gids_.push_back(gid);

            stages_.push_back(std::move(stage));
        }

        if (geteuid() != 0) return;

        constexpr std::array root_only {
            testFUSEAllow,
            testFUSEDeny,
            testVaultPermOverridesAllow,
            testVaultPermOverridesDeny,
            testFUSEGroupPermissions,
            testGroupPermOverrides,
            testFUSEUserOverridesGroupOverride
        };

        for (const auto& function : root_only) {
            auto stage = function();
            validateStage(stage);

            for (const auto& uid : stage.uids) linux_uids_.push_back(uid);
            for (const auto& gid : stage.gids) linux_gids_.push_back(gid);

            stages_.push_back(std::move(stage));
        }
    }
}
