#include "db/DBConnection.hpp"
#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "crypto/secrets/TPMKeyProvider.hpp"
#include "crypto/password/Strength.hpp"
#include "log/Registry.hpp"

#include <fstream>
#include <paths.h>
#include <pqxx/nontransaction>
#include <string_view>

using namespace vh::crypto;

namespace vh::db {

// Bounds (re)connect attempts so an unreachable server can't park a pool caller in connect() for the
// kernel's TCP timeout. libpq's default is to wait forever.
static constexpr int DB_CONNECT_TIMEOUT_SECONDS = 10;

void Connection::configureSession(pqxx::connection& conn) {
    // Every daemon session runs in UTC: naive `timestamp` values are then written and read as UTC (the C++ side
    // parses and prints them as UTC), and timestamptz text carries +00. Before this, a non-UTC server TimeZone made
    // CURRENT_TIMESTAMP land in naive columns as local wall time, read back with a 'Z' (#157).
    // The zone the session started in (database/role/server default) is kept in vaulthalla.database_timezone, so
    // migration 100 can interpret rows written before the switch in that zone.
    pqxx::nontransaction tx(conn);
    const auto original = tx.exec("SELECT current_setting('TimeZone')").one_field().as<std::string>();
    tx.exec("SELECT set_config('vaulthalla.database_timezone', $1, false), set_config('TimeZone', 'UTC', false)",
            pqxx::params{original});
}

static std::optional<std::string> getFirstInitDBPass() {
    const std::filesystem::path f{"/run/vaulthalla/db_password"};

    if (!std::filesystem::exists(f)) {
        log::Registry::runtime()->debug("[seed] No pending db_password file at {}", f.string());
        return std::nullopt;
    }

    std::ifstream in(f);
    if (!in.is_open()) {
        log::Registry::runtime()->warn("[seed] Failed to open db_password file: {}", f.string());
        return std::nullopt;
    }

    std::string pass{};
    if (!(in >> pass)) {
        log::Registry::runtime()->warn("[seed] Invalid contents in {}", f.string());
        return std::nullopt;
    }

    return pass;
}

static void clearPendingDBPassIfPresent() {
    const std::filesystem::path f{"/run/vaulthalla/db_password"};
    if (!std::filesystem::exists(f)) return;

    try {
        std::filesystem::remove(f);
        log::Registry::runtime()->debug("[seed] Consumed and removed pending db_password file {}", f.string());
    } catch (const std::exception& e) {
        log::Registry::runtime()->warn("[seed] Failed to remove {}: {}", f.string(), e.what());
    }
}

static std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::string stripExportPrefix(std::string key) {
    constexpr std::string_view prefix = "export ";
    if (key.starts_with(prefix)) key.erase(0, prefix.size());
    return trim(std::move(key));
}

static std::string stripQuotes(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

static std::optional<std::string> getTestEnvFileValue(const std::string& key) {
    std::ifstream in(paths::getTestEnvPath());
    if (!in.is_open()) return std::nullopt;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#')) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto lineKey = stripExportPrefix(line.substr(0, eq));
        if (lineKey != key) continue;

        return stripQuotes(line.substr(eq + 1));
    }

    return std::nullopt;
}

static std::optional<std::string> getTestEnvValue(const char* key) {
    if (const auto* value = std::getenv(key); value && *value) return std::string(value);
    return getTestEnvFileValue(key);
}

Connection::Connection() : tpmKeyProvider_(
    std::make_unique<secrets::TPMKeyProvider>(paths::testMode ? "test_psql" : "psql")) {
    if (paths::testMode) {
        const auto user = getTestEnvValue("VH_TEST_DB_USER");
        const auto pass = getTestEnvValue("VH_TEST_DB_PASS");
        const auto host = getTestEnvValue("VH_TEST_DB_HOST");
        const auto port = getTestEnvValue("VH_TEST_DB_PORT");
        const auto name = getTestEnvValue("VH_TEST_DB_NAME");

        if (user && pass && host && port && name) {
            DB_CONNECTION_STR =
                "user=" + *user +
                " password=" + *pass +
                " host=" + *host +
                " port=" + *port +
                " dbname=" + *name +
                " connect_timeout=" + std::to_string(DB_CONNECT_TIMEOUT_SECONDS);

            conn_ = std::make_unique<pqxx::connection>(DB_CONNECTION_STR);
            configureSession(*conn_);

            log::Registry::runtime()->info(
                "[DBConnection] Test mode: using connection string from environment variables");
            return;
        }
    }

    const auto initPass = getFirstInitDBPass();
    if (tpmKeyProvider_->sealedExists()) {
        try {
            tpmKeyProvider_->init();
        } catch (const std::exception& e) {
            const auto msg = std::string("Database password failed to initialize: could not unseal the stored DB "
                                         "password with the TPM (sealed under ") + paths::getBackingPath().string() +
                             "): " + e.what();
            log::Registry::runtime()->error("[DBConnection] {}", msg);
            throw std::runtime_error(msg);
        }
        if (initPass) {
            const auto replacement = std::vector<uint8_t>(initPass->begin(), initPass->end());
            tpmKeyProvider_->updateMasterKey(replacement);
            clearPendingDBPassIfPresent();
            log::Registry::runtime()->info("[seed] Reseeded TPM-stored DB password from pending handoff file");
        }
    } else {
        if (!initPass) {
            const auto msg = std::string("Database password failed to initialize: no TPM-sealed DB password under ") +
                             paths::getBackingPath().string() +
                             " and no pending password handoff at /run/vaulthalla/db_password (the sealed secret was "
                             "removed, e.g. by a package purge, or first-time DB setup never ran). Write the "
                             "PostgreSQL role's password to /run/vaulthalla/db_password (owner vaulthalla, mode 0600) "
                             "and restart vaulthalla.service.";
            log::Registry::runtime()->error("[DBConnection] {}", msg);
            throw std::runtime_error(msg);
        }
        tpmKeyProvider_->init(*initPass);
        clearPendingDBPassIfPresent();
    }

    const auto& key = tpmKeyProvider_->getMasterKey();
    const auto password = password::Strength::escape_uri_component({key.begin(), key.end()});

    const auto& config = config::Registry::get();
    const auto db = config.database;
    DB_CONNECTION_STR = "postgresql://" + db.user + ":" + password + "@" + db.host + ":" + std::to_string(db.port) + "/"
                        + db.name + "?connect_timeout=" + std::to_string(DB_CONNECT_TIMEOUT_SECONDS);
    try {
        conn_ = std::make_unique<pqxx::connection>(DB_CONNECTION_STR);
        configureSession(*conn_);
    } catch (const std::exception& e) {
        // libpq's message names the cause (e.g. password authentication failed) and never includes the password.
        const auto msg = "Database connection failed for " + db.user + "@" + db.host + ":" + std::to_string(db.port) +
                         "/" + db.name + ": " + e.what();
        log::Registry::runtime()->error("[DBConnection] {}", msg);
        throw std::runtime_error(msg);
    }
}

Connection::~Connection() { if (conn_ && conn_->is_open()) conn_->close(); }

pqxx::connection& Connection::get() const { return *conn_; }

bool Connection::healthy() const noexcept { return conn_ && conn_->is_open(); }

void Connection::reconnect() {
    auto fresh = std::make_unique<pqxx::connection>(DB_CONNECTION_STR);
    configureSession(*fresh);
    conn_.swap(fresh);
    fresh.reset();

    if (!prepared_) return;

    try {
        initPrepared();
    } catch (...) {
        // An open-but-unprepared session would fail every query by statement name. Close it so healthy()
        // reports false and the pool retries the whole reconnect later.
        conn_->close();
        throw;
    }
}

void Connection::initPrepared() {
    if (!conn_ || !conn_->is_open()) throw std::runtime_error("Database connection is not open");
    prepared_ = true;

    // Auth
    initPreparedUsers();
    initPreparedGroups();
    initPreparedRefreshTokens();

    // RBAC
    initPreparedGlobalVaultRoles();
    initPreparedAdminRoles();
    initPreparedAdminRoleAssignments();
    initPreparedVaultRoles();
    initPreparedVaultRoleAssignments();
    initPreparedPermOverrides();
    initPreparedPermissions();

    // Vaults
    initPreparedVaults();
    initPreparedVaultKeys();
    initPreparedAPIKeys();
    initPreparedVaultActivity();
    initPreparedVaultRecovery();
    initPreparedVaultSecurity();

    // Sync
    initPreparedSync();
    initPreparedSyncEvents();
    initPreparedSyncStats();
    initPreparedSyncThroughput();
    initPreparedSyncConflicts();
    initPreparedSyncConflictArtifacts();
    initPreparedSyncConflictReasons();

    // Filesystem
    initPreparedFsEntries();
    initPreparedFiles();
    initPreparedSymlinks();
    initPreparedDirectories();
    initPreparedOperations();
    initPreparedCache();

    // Share links
    initPreparedShareLinks();
    initPreparedShareSessions();
    initPreparedShareEmailChallenges();
    initPreparedShareUploads();
    initPreparedShareAuditEvents();
    initPreparedShareVaultRoles();
    initPreparedShareStats();

    // Stats
    initPreparedDbStats();
    initPreparedOperationStats();
    initPreparedRetentionStats();
    initPreparedStatsSnapshots();
    initPreparedStatsMetricSamples();

    // Email / notifications
    initPreparedOperatorNotificationDelivery();

    // Dashboard
    initPreparedDashboardPreferences();

    // Admin
    initPreparedSecrets();
    initPreparedWaivers();
}

}
