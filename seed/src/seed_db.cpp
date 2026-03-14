#include "seed_db.hpp"

// Database
#include "db/query/vault/Vault.hpp"
#include "../../include/db/query/rbac/Permission.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/fs/Entry.hpp"
#include "db/Transactions.hpp"

// Types
#include "rbac/permission/Permission.hpp"
#include "rbac/permission/bitmask.hpp"
#include "vault/model/S3Vault.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "../../include/identities/User.hpp"
#include "../../include/identities/Group.hpp"
#include "rbac/role/Base.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "vault/model/Vault.hpp"
#include "fs/model/Directory.hpp"
#include "vault/model/APIKey.hpp"

// Misc
#include "config/Registry.hpp"
#include "vault/APIKeyManager.hpp"
#include "log/Registry.hpp"
#include "runtime/Deps.hpp"
#include "crypto/id/Generator.hpp"
#include "crypto/util/hash.hpp"
#include "auth/SystemUid.hpp"

// Libraries
#include <memory>
#include <version.h>
#include <optional>
#include <fstream>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <paths.h>

using namespace vh::seed;
using namespace vh::config;
using namespace vh::crypto;
using namespace vh::identities::model;
using namespace vh::rbac;
using namespace vh::vault::model;
using namespace vh::crypto;
using namespace vh::sync::model;
using namespace vh::fs::model;

void vh::seed::seed_database() {
    log::Registry::audit()->info("Initializing database for Vaulthalla v{}", VH_VERSION);
    log::Registry::vaulthalla()->debug("Initializing database for Vaulthalla v{}", VH_VERSION);

    initPermissions();
    initRoles();
    initAdmin();
    initAdminGroup();
    initSystemUser();
    initRoot();
    initAdminDefaultVault();

    log::Registry::vaulthalla()->debug("[initdb] Database initialization complete");
    log::Registry::audit()->info("Database initialization complete for Vaulthalla v{}", VH_VERSION);
}

void vh::seed::initPermissions() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing permissions...");

    const std::vector<permission::Permission> userPerms{
        {0, "manage_encryption_keys", "Can manage encryption keys for the system"},
        {1, "manage_admins", "Can manage admin users (create, deactivate, modify)"},
        {2, "manage_users", "Can manage regular users"},
        {3, "manage_groups", "Can create, modify, delete groups"},
        {4, "manage_roles", "Can create, modify, delete roles"},
        {5, "manage_settings", "Can modify system-wide settings"},
        {6, "manage_vaults", "Can create, delete, modify any vault and any vault settings"},
        {7, "manage_api_keys", "Can manage API keys globally"},
        {8, "access_audit_logs", "Can view system audit logs"},
        {9, "create_vaults", "Can create new vaults"}
    };

    const std::vector<permission::Permission> vaultPerms{
        {0, "manage_vault", "Can manage vault settings, including sync and upstream encryption"},
        {1, "manage_access", "Can manage vault roles and access rules"},
        {2, "manage_tags", "Can manage tags for files and directories"},
        {3, "manage_metadata", "Can manage file and directory metadata"},
        {4, "manage_versions", "Can manage file version history"},
        {5, "manage_file_locks", "Can lock or unlock files"},
        {6, "share", "Can create public sharing links"},
        {7, "sync", "Can sync vault data to external/cloud storage"},
        {8, "create", "Can create files or directories and upload files"},
        {9, "download", "Can download files or read file contents"},
        {10, "delete", "Can delete files or directories"},
        {11, "rename", "Can rename files or directories"},
        {12, "move", "Can move files or directories"},
        {13, "list", "Can list directory contents"}
    };

    db::Transactions::exec("initdb::initPermissions", [&](pqxx::work& txn) {
        for (const auto& p : userPerms)
            txn.exec(pqxx::prepped{"insert_raw_permission"},
                pqxx::params{p.bit_position, p.name, p.description, "user"});

        for (const auto& p : vaultPerms)
            txn.exec(pqxx::prepped{"insert_raw_permission"},
                pqxx::params{p.bit_position, p.name, p.description, "vault"});
    });
}

void vh::seed::initRoles() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing roles...");

    std::vector<role::Base> roles{
        {"super_admin", "Root-level system owner with unrestricted access", "user", 0b0000001111111111},
        {"admin", "System administrator with all non-root administrative powers", "user", 0b0000001111111100},
        {"unprivileged", "User with no admin privileges", "user", 0b0000000000000000},
        {"power_user", "Advanced user with full vault level control", "vault", 0b0011111111111111},
        {"user", "Standard user with basic file operations", "vault", 0b0000000111101000},
        {"guest", "Minimal access: can download files and list directories", "vault", 0b0000000000101000},
        {"implicit_deny", "Role that denies all permissions", "vault", 0b0000000000000000}
    };

    db::Transactions::exec("initdb::initRoles", [&](pqxx::work& txn) {
        for (auto& r : roles) {
            r.id = txn.exec(pqxx::prepped{"insert_role"},
                pqxx::params{r.name, r.description, r.type}).one_field().as<unsigned int>();

            txn.exec(pqxx::prepped{"assign_permission_to_role"},
                pqxx::params{r.id, permission::toBitset(r.permissions).to_string()});
        }
    });
}

void vh::seed::initSystemUser() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing system user...");

    // 1) Resolve OS UID of the service account (default: "vaulthalla")
    // If you want it configurable, pull from ConfigRegistry here.
    const std::string systemUsername = "vaulthalla";

    // Use your cached helper (or call uid_for_user directly)
    auth::SystemUid::instance().init(systemUsername);
    const auto sysUid = static_cast<unsigned int>(vh::auth::SystemUid::instance().uid());

    // 2) If a user already exists for this Linux UID, ensure it's marked as system + has super_admin
    try {
        if (const auto existingByUid = db::query::identities::User::getUserByLinuxUID(sysUid); existingByUid) {
            log::Registry::vaulthalla()->info(
                "[initdb] System user already exists for linux_uid={} (name='{}')",
                sysUid, existingByUid->name
            );

            const auto role = db::query::rbac::Permission::getRoleByName("super_admin");

            // If your schema supports updating users, do it.
            // If you don't have update queries yet, you can just return here.
            existingByUid->name = "system";
            existingByUid->email = "no-reply@system";
            existingByUid->linux_uid = sysUid;

            existingByUid->admin = std::make_shared<Admin>();
            existingByUid->admin->id = role->id;
            existingByUid->admin->name = role->name;
            existingByUid->admin->description = role->description;
            existingByUid->admin->type = role->type;
            existingByUid->admin->permissions = role->permissions;

            // If you have an update query, use it. If not, skip.
            // db::query::identities::User::updateUser(existingByUid);
            return;
        }
    } catch (...) {
        // getUserByLinuxUid might throw if not found; ignore and proceed to create
    }

    // 3) If a user exists by name "system" but no UID match, align it
    try {
        if (const auto existingByName = db::query::identities::User::getUserByName("system"); existingByName) {
            log::Registry::vaulthalla()->info(
                "[initdb] Found existing 'system' user (id={}), updating linux_uid to {}",
                existingByName->id, sysUid
            );

            const auto role = db::query::rbac::Permission::getRoleByName("super_admin");

            existingByName->linux_uid = sysUid;
            existingByName->email = "";

            existingByName->admin = std::make_shared<Admin>();
            existingByName->admin->id = role->id;
            existingByName->admin->name = role->name;
            existingByName->admin->description = role->description;
            existingByName->admin->type = role->type;
            existingByName->admin->permissions = role->permissions;

            // If you have an update query, use it. If not, you can delete+recreate, but updating is better.
            // db::query::identities::User::updateUser(existingByName);
            return;
        }
    } catch (...) {
        // getUserByName might throw if not found; ignore and proceed to create
    }

    // 4) Create a fresh system user
    const auto user = std::make_shared<Admin>();
    user->name = "system";
    user->email = "no-reply@system";

    // System account shouldn't be used for interactive login.
    // Still set a password hash to satisfy NOT NULL / validation.
    // Generate a deterministic-ish strong password seed (or truly random if you prefer).
    const auto pwSeed = id::Generator({ .namespace_token = "vaulthalla-system-user" }).generate();
    user->setPasswordHash(hash::password(pwSeed));

    user->linux_uid = sysUid;

    const auto role = db::query::rbac::Permission::getRoleByName("super_admin");
    user->role = std::make_shared<Admin>();
    user->role->id = role->id;
    user->role->name = role->name;
    user->role->description = role->description;
    user->role->type = role->type;
    user->role->permissions = role->permissions;

    db::query::identities::User::createUser(user);

    log::Registry::vaulthalla()->info(
        "[initdb] Created system user mapped to OS account '{}' (linux_uid={})",
        systemUsername, sysUid
    );
}

namespace vh::seed {

static std::optional<unsigned int> loadPendingSuperAdminUid() {
    const std::filesystem::path uidFile{paths::getRuntimePath() / "superadmin_uid"};

    if (!std::filesystem::exists(uidFile)) {
        log::Registry::vaulthalla()->debug("[seed] No pending super-admin UID file at {}", uidFile.string());
        return std::nullopt;
    }

    std::ifstream in(uidFile);
    if (!in.is_open()) {
        log::Registry::vaulthalla()->warn("[seed] Failed to open super-admin UID file: {}", uidFile.string());
        return std::nullopt;
    }

    unsigned int uid{};
    if (!(in >> uid)) {
        log::Registry::vaulthalla()->warn("[seed] Invalid contents in {}", uidFile.string());
        return std::nullopt;
    }

    if (!paths::testMode) {
        try {
            std::filesystem::remove(uidFile);
            log::Registry::vaulthalla()->info("[seed] Consumed and removed pending super-admin UID file (uid={})", uid);
        } catch (const std::exception& e) {
            log::Registry::vaulthalla()->warn("[seed] Failed to remove {}: {}", uidFile.string(), e.what());
        }
    }

    return uid;
}

void initAdmin() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing admin user...");

    const auto user = std::make_shared<Admin>();
    user->name = "admin";
    user->email = "";
    user->setPasswordHash(hash::password("vh!adm1n"));
    user->linux_uid = loadPendingSuperAdminUid();

    const auto role = db::query::rbac::Permission::getRoleByName("super_admin");
    user->role = std::make_shared<Admin>();
    user->role->id = role->id;
    user->role->name = role->name;
    user->role->description = role->description;
    user->role->type = role->type;
    user->role->permissions = role->permissions;

    db::query::identities::User::createUser(user);
}

} // namespace vh::seed


void vh::seed::initAdminGroup() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing admin group...");

    auto group = std::make_shared<Group>();
    group->name = "admin";
    group->description = "Core administrative group for system management";
    group->id = db::query::identities::Group::createGroup(group);

    db::query::identities::Group::addMemberToGroup(group->id, db::query::identities::User::getUserByName("admin")->id);

    group = db::query::identities::Group::getGroupByName("admin");
    if (!group) throw std::runtime_error("Failed to create admin group");

    if (group->members.front()->user->name != "admin") throw std::runtime_error("Admin user not added to admin group");
}

void vh::seed::initAdminDefaultVault() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing admin default vault...");

    const auto vault = std::make_shared<Vault>();
    vault->name = ADMIN_DEFAULT_VAULT_NAME;
    vault->description = "Default vault for the admin user";
    vault->type = VaultType::Local;
    vault->owner_id = 1;
    vault->quota = 0; // No quota for admin vault
    vault->mount_point = id::Generator({ .namespace_token = vault->name }).generate();

    const auto sync = std::make_shared<LocalPolicy>();
    sync->interval = std::chrono::minutes(10);
    sync->conflict_policy = LocalPolicy::ConflictPolicy::Overwrite;

    db::query::vault::Vault::upsertVault(vault, sync);
}

void vh::seed::initRoot() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing root directory...");

    const auto dir = std::make_shared<Directory>();
    dir->name = "/";
    dir->base32_alias = id::Generator( { .namespace_token = "absroot" }).generate();
    dir->created_by = dir->last_modified_by = 1;
    dir->path = "/";
    dir->fuse_path = "/";
    dir->inode = 1;
    dir->mode = 0755; // Standard directory permissions
    dir->is_hidden = false;
    dir->is_system = true;

    db::query::fs::Directory::upsertDirectory(dir);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!db::query::fs::Entry::rootExists()) throw std::runtime_error("Failed to create root directory in database");
    log::Registry::vaulthalla()->info("[initdb] Root directory initialized successfully");
}

void vh::seed::initDevCloudVault() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing development Cloudflare R2 vault...");

    try {
        const std::string prefix = "VAULTHALLA_TEST_R2_";
        const auto accessKey = prefix + "ACCESS_KEY";
        const auto secretKey = prefix + "SECRET_ACCESS_KEY";
        const auto endpoint = prefix + "ENDPOINT";

        auto key = std::make_shared<APIKey>();
        key->user_id = 1; // Default user ID for dev mode
        key->name = "R2 Test Key";
        key->provider = S3Provider::CloudflareR2;
        key->region = "wnam";

        if (std::getenv(accessKey.c_str())) key->access_key = std::getenv(accessKey.c_str());
        else return;

        if (std::getenv(secretKey.c_str())) key->secret_access_key = std::getenv(secretKey.c_str());
        else return;

        if (std::getenv(endpoint.c_str())) key->endpoint = std::getenv(endpoint.c_str());
        else return;

        key->id = runtime::Deps::get().apiKeyManager->addAPIKey(key);

        if (key->id == 0) {
            log::Registry::storage()->error("[StorageManager] Failed to create API key for Cloudflare R2");
            return;
        }

        const auto vault = std::make_shared<S3Vault>();
        vault->name = "R2 Test Vault";
        vault->description = "Test vault for Cloudflare R2 in development mode";
        vault->mount_point = id::Generator({ .namespace_token = vault->name }).generate();
        vault->api_key_id = key->id;
        vault->owner_id = 1;
        vault->bucket = "vaulthalla-test";
        vault->type = VaultType::S3;

        const auto sync = std::make_shared<RemotePolicy>();
        sync->interval = std::chrono::minutes(10);
        sync->conflict_policy = RemotePolicy::ConflictPolicy::KeepLocal;
        sync->strategy = RemotePolicy::Strategy::Sync;

        vault->id = db::query::vault::Vault::upsertVault(vault, sync);

        log::Registry::vaulthalla()->info("[initdb] Created R2 test vault");
    } catch (const std::exception& e) {
        log::Registry::storage()->error("[StorageManager] Error initializing dev Cloudflare R2 vault: {}", e.what());
    }
}
