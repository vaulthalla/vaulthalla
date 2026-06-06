#include "IntegrationsTestRunner.hpp"
#include "cmd/generators.hpp"
#include "fuse/helpers.hpp"
#include "fuse/Builder.hpp"
#include "identities/User.hpp"
#include "fs/model/Path.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "vault/model/Vault.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "rbac/role/Vault.hpp"

using namespace vh::test::integration::fuse;
using namespace vh::rbac;
using namespace vh::identities;

namespace vh::test::integration {

    namespace {
        std::shared_ptr<storage::Engine> makeVaultForOwner(const uint32_t ownerId, const std::string& usage) {
            auto vault = std::make_shared<vault::model::Vault>();
            vault->name = generateVaultName(usage);
            vault->description = "FUSE root listing fixture vault";
            vault->owner_id = ownerId;

            const auto sync = std::make_shared<sync::model::LocalPolicy>();
            sync->interval = std::chrono::minutes(15);
            sync->conflict_policy = sync::model::LocalPolicy::ConflictPolicy::Overwrite;

            const auto storageManager = runtime::Deps::get().storageManager;
            if (!storageManager) throw std::runtime_error("Storage manager not initialized");

            vault = storageManager->addVault(vault, sync);
            const auto engine = storageManager->getEngine(vault->id);
            if (!engine) throw std::runtime_error("Failed to initialize FUSE test vault engine");
            return engine;
        }

        std::string listingNeedle(const std::shared_ptr<storage::Engine>& engine) {
            if (!engine || !engine->vault) return {};
            return engine->vault->effectiveFuseName() + "\n";
        }

        std::vector<std::string> mountedVaultListingNeedles() {
            const auto storageManager = runtime::Deps::get().storageManager;
            if (!storageManager) throw std::runtime_error("Storage manager not initialized");

            std::vector<std::string> names;
            for (const auto& engine : storageManager->getEngines()) {
                auto name = listingNeedle(engine);
                if (!name.empty()) names.push_back(std::move(name));
            }

            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }

        std::vector<std::string> mountedVaultListingNeedlesForOwner(const uint32_t ownerId) {
            const auto storageManager = runtime::Deps::get().storageManager;
            if (!storageManager) throw std::runtime_error("Storage manager not initialized");

            std::vector<std::string> names;
            for (const auto& engine : storageManager->getEngines()) {
                if (!engine || !engine->vault || engine->vault->owner_id != ownerId) continue;
                auto name = listingNeedle(engine);
                if (!name.empty()) names.push_back(std::move(name));
            }

            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }

        void assignVaultRoleToUser(
            const std::shared_ptr<User>& user,
            const uint32_t vaultId,
            const std::string& templateName,
            const std::string& usage
        ) {
            if (!user) throw std::runtime_error("Cannot assign vault role to null user");

            const auto role = db::query::rbac::role::Vault::get(templateName);
            if (!role) throw std::runtime_error("Vault role template not found: " + templateName);

            role->id = 0;
            role->name = generateRoleName(EntityType::VAULT_ROLE, usage);
            role->description = "FUSE root listing fixture role";
            role->assign(user->id, "user", vaultId);

            db::query::rbac::role::Vault::upsert(role);
            db::query::rbac::role::vault::Assignments::assign(role);
            user->roles.vaults[vaultId] = role;
        }
    }

    static TestStage testFUSECRUD() {
        auto builder = Builder::make({
            .name = "CRUD",
            .baseDir = "crud_seed"
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeTestCase({
            .name = "FUSE write (admin)",
            .path = "fuse/write",
            .must_contain = {"OK write"},
            .fn = [=]{ return write_as(*ctx.admin->meta.linux_uid, ctx.hello(), "hello world!\n"); }
        });

        builder.makeTestCase({
            .name = "FUSE chmod no-op compatibility (admin)",
            .path = "fuse/chmod",
            .must_contain = {"OK chmod"},
            .fn = [=]{ return chmod_as(*ctx.admin->meta.linux_uid, ctx.hello(), 0600); }
        });

        builder.makeTestCase({
            .name = "FUSE chmod preserves Vaulthalla attrs (admin)",
            .path = "fuse/stat",
            .must_contain = {"mode=644"},
            .fn = [=]{ return stat_mode_as(*ctx.admin->meta.linux_uid, ctx.hello(), 0644); }
        });

        builder.makeTestCase({
            .name = "FUSE cp -a tree with git metadata (admin)",
            .path = "fuse/cp",
            .must_contain = {"OK cp -a"},
            .fn = [=]{ return cp_preserve_tree_as(*ctx.admin->meta.linux_uid, ctx.root / "copied_tree"); }
        });

        builder.makeTestCase({
            .name = "FUSE read copied git config (admin)",
            .path = "fuse/read",
            .must_contain = {"repositoryformatversion"},
            .fn = [=] { return read_as(*ctx.admin->meta.linux_uid, ctx.root / "copied_tree" / ".git" / "config"); }
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

    static TestStage testFUSERootListingSuperAdmin() {
        auto builder = Builder::make({
            .name = "Root Listing Super Admin",
            .baseDir = "root_listing_super_admin_seed"
        });

        const auto [ctx, subj] = builder.scenario();
        (void)subj;

        (void)makeVaultForOwner(ctx.admin->id, "vault/create/root_listing_super_admin/extra_a");
        (void)makeVaultForOwner(ctx.admin->id, "vault/create/root_listing_super_admin/extra_b");

        builder.makeTestCase({
            .name = "FUSE root ls lists all mounted vaults for super-admin",
            .path = "fuse/ls/root",
            .must_contain = mountedVaultListingNeedles(),
            .fn = [=]{ return ls_as(*ctx.admin->meta.linux_uid, ctx.engine->paths->fuseRoot); }
        });

        return builder.exec();
    }

    static TestStage testFUSERootListingUnprivilegedUser() {
        auto builder = Builder::make({
            .name = "Root Listing Unprivileged User",
            .baseDir = "root_listing_unprivileged_seed"
        });

        builder.makeUser({ .userNameSeed = "user/create/root_listing_unprivileged" });

        const auto [ctx, subj] = builder.scenario();

        (void)makeVaultForOwner(ctx.admin->id, "vault/create/root_listing_unprivileged/admin_a");
        (void)makeVaultForOwner(ctx.admin->id, "vault/create/root_listing_unprivileged/admin_b");

        const auto userVaultA = makeVaultForOwner(subj.user->id, "vault/create/root_listing_unprivileged/user_a");
        const auto userVaultB = makeVaultForOwner(subj.user->id, "vault/create/root_listing_unprivileged/user_b");

        assignVaultRoleToUser(
            subj.user,
            userVaultA->vault->id,
            role::Vault::PowerUser().name,
            "vault_role/create/root_listing_unprivileged/user_a"
        );
        assignVaultRoleToUser(
            subj.user,
            userVaultB->vault->id,
            role::Vault::PowerUser().name,
            "vault_role/create/root_listing_unprivileged/user_b"
        );

        builder.makeTestCase({
            .name = "FUSE root ls hides admin vaults from unprivileged user",
            .path = "fuse/ls/root",
            .must_contain = {
                listingNeedle(userVaultA),
                listingNeedle(userVaultB)
            },
            .must_not_contain = mountedVaultListingNeedlesForOwner(ctx.admin->id),
            .fn = [=]{ return ls_as(subj.uid, ctx.engine->paths->fuseRoot); }
        });

        auto stage = builder.exec();

        db::query::rbac::role::vault::Assignments::unassign(userVaultA->vault->id, "user", subj.user->id);
        db::query::rbac::role::vault::Assignments::unassign(userVaultB->vault->id, "user", subj.user->id);

        return stage;
    }

    static TestStage testFUSEAllow() {
        auto builder = Builder::make({
            .name = "Permissions Allow",
            .baseDir = "perm_allow_seed"
        });

        builder.makeUser({ .userNameSeed = "user/create/allow" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/allow",
            .description = "Vault role with permissions to test allow cases",
        });

        const auto [ctx, subj] = builder.scenario();

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

        builder.makeUser({ .userNameSeed = "user/create/deny" });

        const auto [ctx, subj] = builder.scenario();

        builder.makeTestCase({
            .name = "FUSE deny: ls seed",
            .path = "fuse/ls",
            .expect_exit = ENOENT,
            .fn = [=]{ return ls_as(subj.uid, ctx.base()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: read secret",
            .path = "fuse/read",
            .expect_exit = ENOENT,
            .fn = [=]{ return read_as(subj.uid, ctx.secret()); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: write hax",
            .path = "fuse/write",
            .expect_exit = ENOENT,
            .fn = [=]{ return write_as(subj.uid, ctx.docs() / "hax.txt", "nope\n"); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: chmod note",
            .path = "fuse/chmod",
            .expect_exit = ENOENT,
            .fn = [=]{ return chmod_as(subj.uid, ctx.note(), 0600); }
        });

        builder.makeTestCase({
            .name = "FUSE deny: rm -rf seed",
            .path = "fuse/rmrf",
            .fn = [=]{ return rmrf_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testVaultPermOverridesAllow() {
        auto builder = Builder::make({
            .name = "Vault Permission Overrides Allow",
            .baseDir = "perm_override_allow_seed"
        });

        builder.makeUser({ .userNameSeed = "user/create/override" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::ImplicitDeny().name,
            .roleNameSeed = "vault_role/create/override",
            .description = "Vault role with override",
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::ALLOW,
            .pattern = fs::model::makeAbsolute(ctx.baseDir) / "docs" / "*.txt"
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

        builder.makeUser({ .userNameSeed = "user/create/override_deny" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/override_deny",
            .description = "Vault role with override",
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::DENY,
            .pattern = fs::model::makeAbsolute(ctx.baseDir) / "docs" / "*.txt"
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

    static TestStage testVaultPermOverridesListFilter() {
        auto builder = Builder::make({
            .name = "Vault Permission Overrides List Filter",
            .baseDir = "perm_override_list_filter_seed"
        });

        builder.makeUser({ .userNameSeed = "user/create/override_list_filter" });

        builder.buildAssignVRole({
            .subjectType = TargetSubject::User,
            .templateName = role::Vault::ImplicitDeny().name,
            .roleNameSeed = "vault_role/create/override_list_filter",
            .description = "Vault role with scoped list override",
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeOverride({
            .subjectType = TargetSubject::User,
            .permName = "vault.fs.directories.list",
            .effect = permission::OverrideOpt::ALLOW,
            .pattern = fs::model::makeAbsolute(ctx.baseDir) / "docs"
        });

        builder.makeTestCase({
            .name = "FUSE override list filter: parent shows only allowed child",
            .path = "fuse/ls",
            .must_contain = {"docs\n"},
            .must_not_contain = {"note.txt\n"},
            .fn = [=]{ return ls_as(subj.uid, ctx.base()); }
        });

        return builder.exec();
    }

    static TestStage testFUSEGroupPermissions() {
        auto builder = Builder::make({
            .name = "Group Permissions",
            .baseDir = "group_perm_allow_seed"
        });

        builder.makeUser({ .userNameSeed = "user/create/group_perm" });
        builder.makeGroup("group/create/group_perm");
        builder.addUserToGroup();

        builder.buildAssignVRole({
            .subjectType = TargetSubject::Group,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/group_perm",
            .description = "Vault role for testing group perms",
        });

        const auto [ctx, subj] = builder.scenario();

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

        builder.makeUser({ .userNameSeed = "user/create/group_override" });
        builder.makeGroup("group/create/group_override");
        builder.addUserToGroup();

        builder.buildAssignVRole({
            .subjectType = TargetSubject::Group,
            .templateName = role::Vault::PowerUser().name,
            .roleNameSeed = "vault_role/create/group_override",
            .description = "Vault role for testing group override perms",
        });

        const auto [ctx, subj] = builder.scenario();

        builder.makeOverride({
            .subjectType = TargetSubject::Group,
            .permName = "vault.fs.files.download",
            .effect = permission::OverrideOpt::DENY,
            .pattern = fs::model::makeAbsolute(ctx.baseDir) / "docs" / "*.txt"
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

        const auto [ctx, subj] = builder.scenario();

        const auto pattern = fs::model::makeAbsolute(ctx.baseDir) / "docs" / "*.txt";

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
            .name = "FUSE implicit deny: read note",
            .path = "fuse/read",
            .expect_exit = EACCES,
            .fn = [=]{ return read_as(subj.uid, ctx.note()); }
        });

        builder.makeTestCase({
            .name = "FUSE implicit deny: rm -rf seed",
            .path = "fuse/rmrf",
            .expect_exit = EACCES,
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
            testFUSERootListingSuperAdmin,
            testFUSERootListingUnprivilegedUser,
            testFUSEAllow,
            testFUSEDeny,
            testVaultPermOverridesAllow,
            testVaultPermOverridesDeny,
            testVaultPermOverridesListFilter,
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
