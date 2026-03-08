#include "tests/integrations/include/IntegrationsTestRunner.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "db/query/rbac/Permission.hpp"
#include "db/query/vault/Vault.hpp"
#include "tests/integrations/include/generators.hpp"
#include "rbac/model/VaultRole.hpp"
#include "rbac/model/UserRole.hpp"
#include "rbac/model/PermissionOverride.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/Path.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "identities/model/User.hpp"
#include "identities/model/Group.hpp"
#include "vault/model/Vault.hpp"
#include "seed/include/seed_db.hpp"
#include "runtime/Deps.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "fs/Filesystem.hpp"
#include "tests/integrations/include/fuse_test_helpers.hpp"

using namespace vh::test::cli;
using namespace vh::test::fuse;
using namespace vh::rbac::model;
using namespace vh::identities::model;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using namespace vh::fs::model;

static unsigned int uid_index = 1001;

void IntegrationsTestRunner::runFUSETests() {
    testFUSECRUD();

    // FUSE permission tests require root to change the effective uid
    if (geteuid() == 0) {
        testFUSEAllow();
        testFUSEDeny();
        testVaultPermOverridesAllow();
        testVaultPermOverridesDeny();
        testFUSEGroupPermissions();
        testGroupPermOverrides();
        testFUSEUserOverridesGroupOverride();
    }
}

std::shared_ptr<User> IntegrationsTestRunner::createUser(const unsigned int vaultId, const std::optional<uint16_t>& vaultPerms, const std::vector<std::shared_ptr<PermissionOverride>>& overrides) {
    const auto user = std::make_shared<User>();
    user->name = generateName("user/create");

    const auto userRole = db::query::rbac::Permission::getRoleByName("unprivileged");
    if (!userRole) throw std::runtime_error("Admin role not found");

    user->role = std::make_shared<UserRole>();
    user->role->name = userRole->name;
    user->role->permissions = userRole->permissions;
    user->role->id = userRole->id;
    user->role->description = userRole->description;

    user->linux_uid = uid_index++;
    linux_uids_.push_back(*user->linux_uid);

    if (vaultPerms) {
        const auto vaultRole = std::make_shared<VaultRole>();
        vaultRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create");
        vaultRole->permissions = *vaultPerms;
        vaultRole->description = "Vault role for user " + user->name;
        vaultRole->type = "vault";
        vaultRole->vault_id = vaultId;
        vaultRole->permission_overrides = overrides;
        vaultRole->role_id = db::query::rbac::Permission::addRole(std::static_pointer_cast<Role>(vaultRole));
        user->roles[vaultId] = vaultRole;
    }

    bool nameException = false;

    do {
        try {
            if (nameException) {
                user->name = generateName("user/create");
                nameException = false;
            }
            user->id = db::query::identities::User::createUser(user);
        } catch (const std::exception& e) {
            if (std::string(e.what()).contains("Key (name)=() already exists")) nameException = true;
            else throw std::runtime_error("Failed to create user: " + std::string(e.what()));
        }
    } while (nameException);

    return user;
}

static std::shared_ptr<Engine> createVault() {
    auto vault = std::make_shared<Vault>();
    vault->name = generateVaultName("vault/create");
    vault->description = "Test Vault";
    vault->owner_id = vh::db::query::identities::User::getUserByName("admin")->id;

    if (vault->name.empty()) throw std::runtime_error("Vault name cannot be empty");

    const auto sync = std::make_shared<LocalPolicy>();
    sync->interval = std::chrono::minutes(15);
    sync->conflict_policy = LocalPolicy::ConflictPolicy::Overwrite;

    vault = vh::runtime::Deps::get().storageManager->addVault(vault, sync);
    return vh::runtime::Deps::get().storageManager->getEngine(vault->id);
}

void IntegrationsTestRunner::testFUSECRUD() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin) throw std::runtime_error("Admin user/uid not found");
    if (!admin->linux_uid) throw std::runtime_error("Admin user linux_uid not set");

    const auto vault = db::query::vault::Vault::getVault(std::string(seed::ADMIN_DEFAULT_VAULT_NAME), admin->id);
    if (!vault) throw std::runtime_error("Admin default vault not found");

    const auto engine = runtime::Deps::get().storageManager->getEngine(vault->id);
    if (!engine) throw std::runtime_error("Engine not found for admin vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(vault->name));
    std::cout << "FUSE vault root for '" << vault->name << "':  " << root.string() << std::endl;

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE mkdir (admin)", "fuse/mkdir", /*expect=*/0, {"OK mkdir"}),
        [=]{ return mkdir_as(*admin->linux_uid, root / "crud_test"); }
    });

    steps.push_back({
        make_fuse_case("FUSE write (admin)", "fuse/write", 0, {"OK write"}),
        [=]{ return write_as(*admin->linux_uid, root / "crud_test" / "hello.txt", "hello world!\n"); }
    });

    steps.push_back({
        make_fuse_case("FUSE read (admin)", "fuse/read", 0 /* stdout will contain file data */),
        [=] { return read_as(*admin->linux_uid, root / "crud_test" / "hello.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE rename (admin)", "fuse/rename", 0, {"OK mv"}),
        [=]{ return mv_as(*admin->linux_uid, root / "crud_test" / "hello.txt", root / "crud_test" / "hello2.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE rm -rf (admin)", "fuse/rmrf", 0, {"OK rm -rf"}),
        [=]{ return rmrf_as(*admin->linux_uid, root / "crud_test"); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: CRUD (admin default vault)", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testFUSEAllow() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));

    // Seed files as admin
    seed_vault_tree(*admin->linux_uid, root);

    // Create user with vault perms (power_user)
    const auto powerUserRole = db::query::rbac::Permission::getRoleByName("power_user");
    if (!powerUserRole) throw std::runtime_error("Power user role not found");
    if (powerUserRole->permissions == 0) throw std::runtime_error("Power user role has no permissions");
    const auto user = createUser(engine->vault->id, powerUserRole->permissions, {});
    if (!user || !user->linux_uid) throw std::runtime_error("Failed to create user / uid");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE allow: ls seed", "fuse/ls", 0 /* allow */),
        [=]{ return ls_as(*user->linux_uid, root / "perm_seed"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: read secret", "fuse/read", 0 /* allow */),
        [=]{ return read_as(*user->linux_uid, root / "perm_seed" / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: write user_note", "fuse/write", 0, {"OK write"}),
        [=]{ return write_as(*user->linux_uid, root / "perm_seed" / "docs" / "user_note.txt", "hey\n"); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Permissions Allow", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testFUSEDeny() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));

    // Seed files as admin
    seed_vault_tree(*admin->linux_uid, root);

    // Create user with NO perms
    const auto user = createUser(engine->vault->id);
    if (!user || !user->linux_uid) throw std::runtime_error("Failed to create user / uid");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE deny: read secret", "fuse/read", EACCES /* deny */),
        [=]{ return read_as(*user->linux_uid, root / "perm_seed" / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE deny: write hax", "fuse/write", EACCES /* deny */),
        [=]{ return write_as(*user->linux_uid, root / "perm_seed" / "docs" / "hax.txt", "nope\n"); }
    });

    steps.push_back({
        make_fuse_case("FUSE deny: rm -rf seed", "fuse/rmrf", EACCES /* deny */),
        [=]{ return rmrf_as(*user->linux_uid, root / "perm_seed"); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Permissions Deny", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testVaultPermOverridesAllow() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));
    const std::string baseDir = "perm_override_allow_seed";
    seed_vault_tree(*admin->linux_uid, root, baseDir);

    const auto role = db::query::rbac::Permission::getRoleByName("implicit_deny");
    if (!role) throw std::runtime_error("Implicit deny role not found");
    if (role->permissions > 0) throw std::runtime_error("Implicit deny role has permissions");

    const auto override = std::make_shared<PermissionOverride>();
    const auto perm = db::query::rbac::Permission::getPermissionByName("download");
    if (!perm) throw std::runtime_error("Download permission not found");
    override->permission = *perm;
    override->effect = OverrideOpt::ALLOW;
    override->patternStr = R"(^/perm_override_allow_seed/docs/[^/]+\.txt$)";
    override->pattern    = std::regex(override->patternStr, std::regex::ECMAScript);

    const auto vRole = std::make_shared<VaultRole>();
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create/override");
    vRole->permissions = role->permissions; // 0
    vRole->description = "Vault role with override";
    vRole->type = "vault";
    vRole->vault_id = engine->vault->id;
    vRole->permission_overrides = { override };
    vRole->role_id = role->id;

    const auto userRole = db::query::rbac::Permission::getRoleByName("unprivileged");
    if (!userRole) throw std::runtime_error("Admin role not found");

    const auto user = std::make_shared<User>();
    user->name = generateName("user/create/override");
    user->role = std::make_shared<UserRole>(*userRole);
    user->linux_uid = uid_index++;
    linux_uids_.push_back(*user->linux_uid);
    user->roles[engine->vault->id] = vRole;
    user->id = db::query::identities::User::createUser(user);

    const auto verify = db::query::identities::User::getUserById(user->id);
    if (!verify) throw std::runtime_error("Failed to verify created user");
    if (!verify->linux_uid) throw std::runtime_error("Created user linux_uid not set");
    if (verify->roles.size() != 1) throw std::runtime_error("Created user roles size != 1");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE override allow: read secret", "fuse/read", 0 /* allow due to override */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE deny: read note", "fuse/read", EACCES /* deny */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "note.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE deny: rm -rf seed", "fuse/rmrf", EACCES /* deny */),
        [=]{ return rmrf_as(*user->linux_uid, root / baseDir); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Vault Permission Overrides Allow", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testVaultPermOverridesDeny() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));
    const std::string baseDir = "perm_override_deny_seed";
    seed_vault_tree(*admin->linux_uid, root, baseDir);

    const auto role = db::query::rbac::Permission::getRoleByName("power_user");
    if (!role) throw std::runtime_error("Power user role not found");
    if (role->permissions == 0) throw std::runtime_error("Power user role has no permissions");

    const auto override = std::make_shared<PermissionOverride>();
    const auto perm = db::query::rbac::Permission::getPermissionByName("download");
    if (!perm) throw std::runtime_error("Download permission not found");
    override->permission = *perm;
    override->effect = OverrideOpt::DENY;
    override->patternStr = R"(^/perm_override_deny_seed/docs/[^/]+\.txt$)";
    override->pattern    = std::regex(override->patternStr, std::regex::ECMAScript);

    const auto vRole = std::make_shared<VaultRole>();
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create/override_deny");
    vRole->permissions = role->permissions; // 0
    vRole->description = "Vault role with override";
    vRole->type = "vault";
    vRole->vault_id = engine->vault->id;
    vRole->permission_overrides = { override };
    vRole->role_id = role->id;

    const auto userRole = db::query::rbac::Permission::getRoleByName("unprivileged");
    if (!userRole) throw std::runtime_error("Admin role not found");

    const auto user = std::make_shared<User>();
    user->name = generateName("user/create/override_deny");
    user->role = std::make_shared<UserRole>(*userRole);
    user->linux_uid = uid_index++;
    linux_uids_.push_back(*user->linux_uid);
    user->roles[engine->vault->id] = vRole;
    user->id = db::query::identities::User::createUser(user);

    const auto verify = db::query::identities::User::getUserById(user->id);
    if (!verify) throw std::runtime_error("Failed to verify created user");
    if (!verify->linux_uid) throw std::runtime_error("Created user linux_uid not set");
    if (verify->roles.size() != 1) throw std::runtime_error("Created user roles size != 1");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE override deny: read secret", "fuse/read", EACCES /* deny due to override */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: read note", "fuse/read", 0 /* allow due to power_user */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "note.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: rm -rf seed", "fuse/rmrf", 0 /* allow due to power_user */),
        [=]{ return rmrf_as(*user->linux_uid, root / baseDir); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Vault Permission Overrides Deny", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testFUSEGroupPermissions() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));

    const std::string baseDir = "group_seed";
    seed_vault_tree(*admin->linux_uid, root, baseDir);

    const auto user = createUser(engine->vault->id);
    if (!user || !user->linux_uid) throw std::runtime_error("Failed to create user / uid");

    const auto powerUserRole = db::query::rbac::Permission::getRoleByName("power_user");
    if (!powerUserRole) throw std::runtime_error("Power user role not found");
    if (powerUserRole->permissions == 0) throw std::runtime_error("Power user role has no permissions");

    const auto group = std::make_shared<Group>();
    group->name = generateName("group/create");
    group->description = "Test group for FUSE perms";
    group->id = db::query::identities::Group::createGroup(group);

    db::query::identities::Group::addMemberToGroup(group->id, user->id);

    const auto vRole = std::make_shared<VaultRole>();
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create");
    vRole->role_id = powerUserRole->id;
    vRole->permissions = powerUserRole->permissions;
    vRole->description = "Vault role for testing group perms";
    vRole->vault_id = engine->vault->id;
    vRole->subject_type = "group";
    vRole->subject_id = group->id;
    vRole->type = "vault";

    db::query::rbac::Permission::assignVaultRole(vRole);

    const auto verify = db::query::identities::User::getUserById(user->id);
    if (!verify) throw std::runtime_error("Failed to verify created user");
    if (!verify->linux_uid) throw std::runtime_error("Created user linux_uid not set");
    if (!verify->roles.empty()) throw std::runtime_error("Created user roles size != 0");
    if (verify->group_roles.size() != 1) throw std::runtime_error("Created user group_roles size != 1");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE allow: ls seed", "fuse/ls", 0 /* allow */),
        [=]{ return ls_as(*user->linux_uid, root / baseDir); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: read secret", "fuse/read", 0 /* allow */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: write user_note", "fuse/write", 0, {"OK write"}),
        [=]{ return write_as(*user->linux_uid, root / baseDir / "docs" / "user_note.txt", "hey\n"); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Group Permissions Allow", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testGroupPermOverrides() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));
    const std::string baseDir = "group_perm_override_deny_seed";
    seed_vault_tree(*admin->linux_uid, root, baseDir);

    const auto role = db::query::rbac::Permission::getRoleByName("power_user");
    if (!role) throw std::runtime_error("Power user role not found");
    if (role->permissions == 0) throw std::runtime_error("Power user role has no permissions");

    const auto override = std::make_shared<PermissionOverride>();
    const auto perm = db::query::rbac::Permission::getPermissionByName("download");
    if (!perm) throw std::runtime_error("Download permission not found");
    override->permission = *perm;
    override->effect = OverrideOpt::DENY;
    override->patternStr = R"(^/group_perm_override_deny_seed/docs/[^/]+\.txt$)";
    override->pattern    = std::regex(override->patternStr, std::regex::ECMAScript);

    const auto group = std::make_shared<Group>();
    group->name = generateName("group/create/override_deny");
    group->description = "Test group for FUSE perms";
    group->id = db::query::identities::Group::createGroup(group);

    const auto user = createUser(engine->vault->id);

    db::query::identities::Group::addMemberToGroup(group->id, user->id);

    const auto vRole = std::make_shared<VaultRole>();
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create/override_deny");
    vRole->permissions = role->permissions;
    vRole->description = "Vault role with override";
    vRole->type = "vault";
    vRole->vault_id = engine->vault->id;
    vRole->permission_overrides = { override };
    vRole->role_id = role->id;
    vRole->subject_type = "group";
    vRole->subject_id = group->id;

    db::query::rbac::Permission::assignVaultRole(vRole);

    const auto verify = db::query::identities::User::getUserById(user->id);
    if (!verify) throw std::runtime_error("Failed to verify created user");
    if (!verify->linux_uid) throw std::runtime_error("Created user linux_uid not set");
    if (verify->group_roles.size() != 1) throw std::runtime_error("Created group roles size != 1");
    if (verify->getRole(engine->vault->id)->permission_overrides.empty()) throw std::runtime_error("Created user role overrides size != 1");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE override deny: read secret", "fuse/read", EACCES /* deny due to override */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: read note", "fuse/read", 0 /* allow due to power_user */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "note.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: rm -rf seed", "fuse/rmrf", 0 /* allow due to power_user */),
        [=]{ return rmrf_as(*user->linux_uid, root / baseDir); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: Group Vault Permission Overrides Deny", cases });
    validateStage(stages_.back());
}

void IntegrationsTestRunner::testFUSEUserOverridesGroupOverride() {
    const auto admin = db::query::identities::User::getUserByName("admin");
    if (!admin || !admin->linux_uid) throw std::runtime_error("Admin user/uid not found");

    const auto engine = createVault();
    if (!engine) throw std::runtime_error("Failed to create vault");

    const auto root = std::filesystem::path(engine->paths->fuseRoot / to_snake_case(engine->vault->name));
    const std::string baseDir = "user_group_perm_override_seed";
    seed_vault_tree(*admin->linux_uid, root, baseDir);

    const auto role = std::make_shared<Role>();
    role->name = "override_only";
    role->description = "Role with no base perms, just override";
    role->permissions = 0;
    role->type = "vault";
    role->id = db::query::rbac::Permission::addRole(role);

    const auto override = std::make_shared<PermissionOverride>();
    const auto perm = db::query::rbac::Permission::getPermissionByName("download");
    if (!perm) throw std::runtime_error("Download permission not found");
    override->permission = *perm;
    override->effect = OverrideOpt::DENY;
    override->patternStr = R"(^/user_group_perm_override_seed/docs/[^/]+\.txt$)";
    override->pattern    = std::regex(override->patternStr, std::regex::ECMAScript);

    const auto group = std::make_shared<Group>();
    group->name = generateName("group/create/override_deny");
    group->description = "Test group for FUSE perms";
    group->id = db::query::identities::Group::createGroup(group);

    const auto vRole = std::make_shared<VaultRole>();
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create/override_deny");
    vRole->permissions = role->permissions;
    vRole->description = "Vault role with override";
    vRole->type = "vault";
    vRole->vault_id = engine->vault->id;
    vRole->permission_overrides = { override };
    vRole->role_id = role->id;
    vRole->subject_type = "group";
    vRole->subject_id = group->id;

    db::query::rbac::Permission::assignVaultRole(vRole);

    // Manipulate the override to be ALLOW for the user

    override->effect = OverrideOpt::ALLOW; // User override should take precedence

    vRole->subject_type = "user";
    vRole->subject_id = 0; // Will be set after user creation
    vRole->name = generateRoleName(EntityType::VAULT_ROLE, "vault_role/create/override_deny_user");
    vRole->permissions = role->permissions; // 0
    vRole->permission_overrides = { override };

    const auto userRole = db::query::rbac::Permission::getRoleByName("unprivileged");
    if (!userRole) throw std::runtime_error("Admin role not found");

    const auto user = std::make_shared<User>();
    user->name = generateName("user/create/override_deny");
    user->role = std::make_shared<UserRole>(*userRole);
    user->linux_uid = uid_index++;
    linux_uids_.push_back(*user->linux_uid);
    user->roles[engine->vault->id] = vRole;
    user->id = db::query::identities::User::createUser(user);

    db::query::identities::Group::addMemberToGroup(group->id, user->id);

    const auto verify = db::query::identities::User::getUserById(user->id);
    if (!verify) throw std::runtime_error("Failed to verify created user");
    if (!verify->linux_uid) throw std::runtime_error("Created user linux_uid not set");
    if (verify->roles.size() != 1) throw std::runtime_error("Created user roles size != 1, actual: " + std::to_string(verify->roles.size()));
    if (verify->group_roles.size() != 1) throw std::runtime_error("Created group roles size != 1, actual: " + std::to_string(verify->group_roles.size()));
    if (verify->getRole(engine->vault->id)->permission_overrides.empty()) throw std::runtime_error("Created user role overrides size != 1");

    std::vector<FuseStep> steps;

    steps.push_back({
        make_fuse_case("FUSE override deny: read secret", "fuse/read", 0 /* allow due to user override */),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "docs" / "secret.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: read note", "fuse/read", EACCES),
        [=]{ return read_as(*user->linux_uid, root / baseDir / "note.txt"); }
    });

    steps.push_back({
        make_fuse_case("FUSE allow: rm -rf seed", "fuse/rmrf", EACCES),
        [=]{ return rmrf_as(*user->linux_uid, root / baseDir); }
    });

    const auto cases = run_fuse_steps(steps);
    stages_.push_back(TestStage{ "FUSE: User Perm Overrides Group Perm Override", cases });
    validateStage(stages_.back());
}
