#include "seed/include/seed_db.hpp"

// Database
#include "db/query/vault/Vault.hpp"
#include "db/query/rbac/Permission.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/fs/Entry.hpp"
#include "db/Transactions.hpp"

// Types
#include "rbac/permission/Permission.hpp"
#include "vault/model/S3Vault.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "identities/User.hpp"
#include "identities/Group.hpp"
#include "rbac/role/Vault.hpp"
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
using namespace vh::identities;
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

    db::Transactions::exec("initdb::initPermissions", [&](pqxx::work& txn) {
        for (const auto& p : role::Admin().toPermissions())
            txn.exec(pqxx::prepped{"insert_raw_permission"},
                pqxx::params{p.qualified_name, p.description, "admin", p.bit_position});

        for (const auto& p : role::Vault().toPermissions())
            txn.exec(pqxx::prepped{"insert_raw_permission"},
                pqxx::params{p.qualified_name, p.description, "vault", p.bit_position});
    });
}

void vh::seed::initRoles() {
    log::Registry::vaulthalla()->debug("[initdb] Initializing roles...");

    const std::vector adminRoles{
        role::Admin::None(),
        role::Admin::Auditor(),
        role::Admin::Support(),
        role::Admin::IdentityAdmin(),
        role::Admin::PlatformOperator(),
        role::Admin::VaultAdmin(),
        role::Admin::SecurityAdmin(),
        role::Admin::OrgAdmin(),
        role::Admin::SuperAdmin(),
        role::Admin::KeyCustodian()
    };

    const std::vector vaultRoles{
        role::Vault::ImplicitDeny(),
        role::Vault::Guest(),
        role::Vault::Reader(),
        role::Vault::Contributor(),
        role::Vault::Editor(),
        role::Vault::Manager(),
        role::Vault::PowerUser()
    };

    db::Transactions::exec("initdb::initRoles", [&](pqxx::work& txn) {
        for (const auto& role : adminRoles)
            txn.exec(
            pqxx::prepped{"admin_role_upsert_by_name"},
            pqxx::params{
                role.name,
                role.description,
                role.identities.toBitString(),
                role.audits.toBitString(),
                role.settings.toBitString(),
                role.roles.toBitString(),
                role.vaults.toBitString(),
                role.keys.toBitString()
            }
        );

        for (const auto& role : vaultRoles)
            txn.exec(
                pqxx::prepped{"vault_role_upsert_by_name"},
                pqxx::params{
                    role.name,
                    role.description,
                    role.fs.files.toBitString(),
                    role.fs.directories.toBitString(),
                    role.sync.toBitString(),
                    role.roles.toBitString(),
                }
            );
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

            // If your schema supports updating users, do it.
            // If you don't have update queries yet, you can just return here.
            existingByUid->name = "system";
            existingByUid->email = "no-reply@system";
            existingByUid->meta.linux_uid = sysUid;

            existingByUid->roles.admin = db::query::rbac::role::Admin::get("super_admin");
            existingByUid->roles.admin->user_id = existingByUid->id;

            db::query::identities::User::updateUser(existingByUid);
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

            existingByName->meta.linux_uid = sysUid;
            existingByName->email = "";

            existingByName->roles.admin = db::query::rbac::role::Admin::get("super_admin");
            existingByName->roles.admin->user_id = existingByName->id;

            db::query::identities::User::updateUser(existingByName);
            return;
        }
    } catch (...) {
        // getUserByName might throw if not found; ignore and proceed to create
    }

    // 4) Create a fresh system user
    const auto user = std::make_shared<User>();
    user->name = "system";
    user->email = "no-reply@system";

    // System account shouldn't be used for interactive login.
    // Still set a password hash to satisfy NOT NULL / validation.
    // Generate a deterministic-ish strong password seed (or truly random if you prefer).
    const auto pwSeed = id::Generator({ .namespace_token = "vaulthalla-system-user" }).generate();
    user->setPasswordHash(hash::password(pwSeed));

    user->meta.linux_uid = sysUid;

    user->roles.admin = db::query::rbac::role::Admin::get("super_admin");
    user->roles.admin->user_id = user->id;

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

    const auto user = std::make_shared<User>();
    user->name = "admin";
    user->email = "";
    user->setPasswordHash(hash::password("vh!adm1n"));
    user->meta.linux_uid = loadPendingSuperAdminUid();

    user->roles.admin = db::query::rbac::role::Admin::get("super_admin");
    user->roles.admin->user_id = user->id;

    db::query::identities::User::createUser(user);
}

}


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
