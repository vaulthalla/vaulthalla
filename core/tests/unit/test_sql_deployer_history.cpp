// Regression guard for migrations that were edited in place after shipping (P0: 1.5.x -> 1.6.x upgrades
// crash-looped on "Migration file was modified after being applied: 060_acl.sql").
//
// Non-DB tests always run. DB-backed tests need VH_TEST_DB_{USER,PASS,HOST,PORT,NAME} and run every scenario
// inside a transaction that is rolled back, so they leave the test database exactly as they found it (apart from
// bringing it up to the current schema once, which init_tables_if_not_exists() does in other suites too).

#include "seed/include/SqlDeployer.hpp"
#include "rbac/role/Admin.hpp"

#include <paths.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh_sql_deployer_history_test {

namespace migseed = vh::db::seed;
namespace mfs = std::filesystem;

// The repo's deploy/psql (what test mode uses), without flipping global test-mode state for other suites.
mfs::path repoPsqlDir() { return VH_PSQL_TEST_SCHEMAS_PATH; }

std::string currentHash(const std::string& filename) {
    return migseed::sha256Hex(migseed::readFileToString(repoPsqlDir() / filename));
}

std::optional<std::string> env(const char* key) {
    if (const auto* v = std::getenv(key); v && *v) return std::string(v);
    return std::nullopt;
}

std::optional<std::string> testDbConnectionString() {
    const auto user = env("VH_TEST_DB_USER");
    const auto pass = env("VH_TEST_DB_PASS");
    const auto host = env("VH_TEST_DB_HOST");
    const auto port = env("VH_TEST_DB_PORT");
    const auto name = env("VH_TEST_DB_NAME");
    if (!user || !pass || !host || !port || !name) return std::nullopt;
    return "user=" + *user + " password=" + *pass + " host=" + *host + " port=" + *port + " dbname=" + *name;
}

std::vector<vh::rbac::role::Admin> builtInAdminRoles() {
    using vh::rbac::role::Admin;
    return {
        Admin::None(), Admin::Auditor(), Admin::Support(), Admin::IdentityAdmin(), Admin::PlatformOperator(),
        Admin::VaultAdmin(), Admin::SecurityAdmin(), Admin::OrgAdmin(), Admin::SuperAdmin(), Admin::KeyCustodian()
    };
}

bool columnExists(pqxx::work& txn, const std::string& table, const std::string& column) {
    return !txn.exec(
        "SELECT 1 FROM information_schema.columns "
        "WHERE table_schema = current_schema() AND table_name = $1 AND column_name = $2",
        pqxx::params{table, column}
    ).empty();
}

std::string recordedHash(pqxx::work& txn, const std::string& filename) {
    return txn.exec("SELECT sha256 FROM schema_migrations WHERE filename = $1", pqxx::params{filename})
        .one_row()[0].as<std::string>();
}

bool reconciled(const migseed::SqlDeployReport& r, const std::string& filename) {
    return std::ranges::any_of(r.reconciled, [&](const auto& m) { return m.filename == filename; });
}

bool applied(const migseed::SqlDeployReport& r, const std::string& filename) {
    return std::ranges::find(r.applied, filename) != r.applied.end();
}

// Helpers above and the tests below share this namespace (unity build: avoid clashing with other suites).

// ---------------------------------------------------------------------------------------------------------------
// Allowlist shape (no DB)
// ---------------------------------------------------------------------------------------------------------------

TEST(SqlDeployerHistory, AcceptsOnlyListedHistoricalChecksums) {
    EXPECT_TRUE(migseed::isAcceptedHistoricalChecksum(
        "060_acl.sql", "c154cd7e10f7931f29c12b9221c90f66d6b953cfb5e3dc15ea50f0940e5d4220"));
    EXPECT_TRUE(migseed::isAcceptedHistoricalChecksum(
        "020_vaults.sql", "64170212eb3f9fbe0f7c949791b953f0376a1f2bcf66b3dec7afc5db6325d570"));
    EXPECT_TRUE(migseed::isAcceptedHistoricalChecksum(
        "082_stats_metric_samples.sql", "720caaa27c0d51635fe3f459a8426f019d3f0dbdc0b7ae176d283309933a7f53"));

    // Hash belongs to 060, not 020: acceptance is per file.
    EXPECT_FALSE(migseed::isAcceptedHistoricalChecksum(
        "020_vaults.sql", "c154cd7e10f7931f29c12b9221c90f66d6b953cfb5e3dc15ea50f0940e5d4220"));
    EXPECT_FALSE(migseed::isAcceptedHistoricalChecksum("060_acl.sql", std::string(64, '0')));
    EXPECT_FALSE(migseed::isAcceptedHistoricalChecksum("060_acl.sql", ""));
}

TEST(SqlDeployerHistory, EveryEntryNamesAnExistingFileAndForwardMigration) {
    for (const auto& e : migseed::kHistoricalMigrationChecksums) {
        const std::string filename{e.filename};
        SCOPED_TRACE(filename);

        ASSERT_TRUE(mfs::exists(repoPsqlDir() / filename));
        EXPECT_EQ(e.sha256.size(), 64u);
        EXPECT_NE(std::string(e.sha256), currentHash(filename))
            << "a historical checksum must differ from the current file";

        const std::string note{e.note};
        const auto pos = note.find("forward: ");
        ASSERT_NE(pos, std::string::npos) << "note must name the forward migration that owns the delta";
        const auto start = pos + std::string("forward: ").size();
        const auto forward = note.substr(start, note.find(')', start) - start);
        EXPECT_TRUE(mfs::exists(repoPsqlDir() / forward)) << forward;
        EXPECT_GT(forward, filename) << "forward migration must sort after the edited file";
    }
}

// ---------------------------------------------------------------------------------------------------------------
// DB-backed upgrade simulations
// ---------------------------------------------------------------------------------------------------------------

class SqlDeployerHistoryDb : public ::testing::Test {
protected:
    inline static std::optional<std::string> conninfo;
    inline static std::string skipReason;

    static void SetUpTestSuite() {
        conninfo = testDbConnectionString();
        if (!conninfo) {
            skipReason = "VH_TEST_DB_* not set";
            return;
        }

        // Bring the test DB to the current schema (committed), exactly like init_tables_if_not_exists().
        pqxx::connection conn(*conninfo);
        pqxx::work txn(conn);
        migseed::SqlDeployer::ensureMigrationsTable(txn);
        migseed::SqlDeployer::applyDir(txn, repoPsqlDir());
        txn.commit();
    }

    void SetUp() override {
        if (!conninfo) GTEST_SKIP() << skipReason;
    }

    // Runs `fn` in a transaction that is always rolled back.
    static void inRolledBackTxn(const std::function<void(pqxx::work&)>& fn) {
        pqxx::connection conn(*conninfo);
        pqxx::work txn(conn);
        fn(txn);
        txn.abort();
    }
};

TEST_F(SqlDeployerHistoryDb, UpgradeFromPre160AclChecksumSucceedsAndAddsGatewayColumn) {
    inRolledBackTxn([](pqxx::work& txn) {
        const auto oldHash = std::string("c154cd7e10f7931f29c12b9221c90f66d6b953cfb5e3dc15ea50f0940e5d4220");

        // Recreate the state of a database initialized by <= 1.5.1: old 060 shape (no s3_gateway_permissions),
        // old 060 checksum recorded, 097 never applied, built-in admin roles seeded without gateway bits.
        txn.exec("ALTER TABLE admin_role DROP COLUMN s3_gateway_permissions");
        txn.exec("UPDATE schema_migrations SET sha256 = $1 WHERE filename = '060_acl.sql'", pqxx::params{oldHash});
        txn.exec("DELETE FROM schema_migrations WHERE filename = '097_admin_role_s3_gateway_permissions.sql'");

        for (const auto& role : builtInAdminRoles()) {
            txn.exec(R"SQL(
                INSERT INTO admin_role (name, description, identity_permissions, audit_permissions,
                                        settings_permissions, roles_permissions, vaults_permissions, keys_permissions)
                VALUES ($1, $2, $3::bit(32), $4::bit(8), $5::bit(64), $6::bit(16), $7::bit(32), $8::bit(32))
                ON CONFLICT (name) DO NOTHING
            )SQL", pqxx::params{
                role.name, role.description, role.identities.toBitString(), role.audits.toBitString(),
                role.settings.toBitString(), role.roles.toBitString(), role.vaults.toBitString(),
                role.keys.toBitString()
            });
        }
        txn.exec(R"SQL(
            INSERT INTO admin_role (name, description, identity_permissions, audit_permissions,
                                    settings_permissions, roles_permissions, vaults_permissions, keys_permissions)
            VALUES ('claude_test_custom_role', 'custom', B'0'::bit(32), B'0'::bit(8), B'0'::bit(64),
                    B'0'::bit(16), B'0'::bit(32), B'0'::bit(32))
        )SQL");
        ASSERT_FALSE(columnExists(txn, "admin_role", "s3_gateway_permissions"));

        migseed::SqlDeployReport report;
        ASSERT_NO_THROW(report = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));

        EXPECT_TRUE(reconciled(report, "060_acl.sql"));
        EXPECT_TRUE(applied(report, "097_admin_role_s3_gateway_permissions.sql"));
        EXPECT_FALSE(applied(report, "060_acl.sql")) << "060 must not be re-executed";
        EXPECT_EQ(recordedHash(txn, "060_acl.sql"), currentHash("060_acl.sql"));

        ASSERT_TRUE(columnExists(txn, "admin_role", "s3_gateway_permissions"));
        const auto col = txn.exec(R"SQL(
            SELECT data_type, character_maximum_length, is_nullable, column_default
            FROM information_schema.columns
            WHERE table_schema = current_schema() AND table_name = 'admin_role'
              AND column_name = 's3_gateway_permissions'
        )SQL").one_row();
        EXPECT_EQ(col[0].as<std::string>(), "bit");
        EXPECT_EQ(col[1].as<int>(), 8);
        EXPECT_EQ(col[2].as<std::string>(), "NO");
        EXPECT_NE(col[3].as<std::string>().find("00000000"), std::string::npos);

        // Built-in templates get the same bits a fresh install seeds; custom roles stay least-privilege.
        for (const auto& role : builtInAdminRoles()) {
            SCOPED_TRACE(role.name);
            const auto bits = txn.exec("SELECT s3_gateway_permissions::text FROM admin_role WHERE name = $1",
                                       pqxx::params{role.name}).one_row()[0].as<std::string>();
            EXPECT_EQ(bits, role.s3Gateway.toBitString());
        }
        EXPECT_EQ(txn.exec("SELECT s3_gateway_permissions::text FROM admin_role WHERE name = 'claude_test_custom_role'")
                      .one_row()[0].as<std::string>(), "00000000");

        // Second run is a clean no-op with strict checks back in force.
        migseed::SqlDeployReport again;
        ASSERT_NO_THROW(again = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));
        EXPECT_TRUE(again.applied.empty());
        EXPECT_TRUE(again.reconciled.empty());
    });
}

TEST_F(SqlDeployerHistoryDb, ForwardMigrationIsNoOpOnDatabasesThatRanNewAcl) {
    inRolledBackTxn([](pqxx::work& txn) {
        // A >= 1.6.0 database where an operator deliberately changed a built-in role's gateway bits.
        txn.exec(R"SQL(
            INSERT INTO admin_role (name, description, identity_permissions, audit_permissions,
                                    settings_permissions, roles_permissions, vaults_permissions, keys_permissions,
                                    s3_gateway_permissions)
            VALUES ('super_admin', 'x', B'0'::bit(32), B'0'::bit(8), B'0'::bit(64), B'0'::bit(16), B'0'::bit(32),
                    B'0'::bit(32), B'00000000')
            ON CONFLICT (name) DO UPDATE SET s3_gateway_permissions = B'00000000'
        )SQL");
        txn.exec("DELETE FROM schema_migrations WHERE filename = '097_admin_role_s3_gateway_permissions.sql'");

        migseed::SqlDeployReport report;
        ASSERT_NO_THROW(report = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));
        EXPECT_TRUE(applied(report, "097_admin_role_s3_gateway_permissions.sql"));
        EXPECT_TRUE(report.reconciled.empty());
        EXPECT_EQ(txn.exec("SELECT s3_gateway_permissions::text FROM admin_role WHERE name = 'super_admin'")
                      .one_row()[0].as<std::string>(), "00000000");
    });
}

// #166: an upgraded database gets admin_role.stats_permissions with the same grants a fresh install seeds, and
// every role that passed the old isAdmin() stats gate keeps stats access.
TEST_F(SqlDeployerHistoryDb, StatsPermissionMigrationMatchesFreshSeedAndKeepsOldGateHolders) {
    inRolledBackTxn([](pqxx::work& txn) {
        // A <= 1.10.x database: no stats column, 105 never applied, no catalog row.
        txn.exec("ALTER TABLE admin_role DROP COLUMN stats_permissions");
        txn.exec("DELETE FROM schema_migrations WHERE filename = '105_admin_stats_permission.sql'");
        txn.exec("DELETE FROM permission WHERE name = 'admin.stats.view' AND category = 'admin'");

        const auto upsert = [&](const std::string& name, const vh::rbac::role::Admin& bits) {
            txn.exec(R"SQL(
                INSERT INTO admin_role (name, description, identity_permissions, audit_permissions,
                                        settings_permissions, roles_permissions, vaults_permissions, keys_permissions,
                                        s3_gateway_permissions)
                VALUES ($1, 'x', $2::bit(32), $3::bit(8), $4::bit(64), $5::bit(16), $6::bit(32), $7::bit(32), $8::bit(8))
                ON CONFLICT (name) DO UPDATE SET
                    identity_permissions = EXCLUDED.identity_permissions, audit_permissions = EXCLUDED.audit_permissions,
                    settings_permissions = EXCLUDED.settings_permissions, roles_permissions = EXCLUDED.roles_permissions,
                    vaults_permissions = EXCLUDED.vaults_permissions, keys_permissions = EXCLUDED.keys_permissions,
                    s3_gateway_permissions = EXCLUDED.s3_gateway_permissions
            )SQL", pqxx::params{
                name, bits.identities.toBitString(), bits.audits.toBitString(), bits.settings.toBitString(),
                bits.roles.toBitString(), bits.vaults.toBitString(), bits.keys.toBitString(),
                bits.s3Gateway.toBitString()
            });
        };
        for (const auto& role : builtInAdminRoles()) upsert(role.name, role);

        // The old gate: admin.identities.admins.delete AND admin.vaults.admin.remove. The migration reads those two
        // bits by position, so build them from the permission model rather than hard-coding them here.
        vh::rbac::role::Admin oldGate;
        oldGate.identities.admins.grant(vh::rbac::permission::admin::identities::IdentityPermissions::Delete);
        oldGate.vaults.admin.grant(vh::rbac::permission::admin::VaultPermissions::Remove);
        upsert("claude_test_old_gate_role", oldGate);
        vh::rbac::role::Admin halfGate;
        halfGate.identities.admins.grant(vh::rbac::permission::admin::identities::IdentityPermissions::Delete);
        upsert("claude_test_half_gate_role", halfGate);
        upsert("claude_test_plain_role", vh::rbac::role::Admin{});

        migseed::SqlDeployReport report;
        ASSERT_NO_THROW(report = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));
        EXPECT_TRUE(applied(report, "105_admin_stats_permission.sql"));

        const auto bitsOf = [&](const std::string& name) {
            return txn.exec("SELECT stats_permissions::text FROM admin_role WHERE name = $1", pqxx::params{name})
                .one_row()[0].as<std::string>();
        };
        for (const auto& role : builtInAdminRoles()) {
            SCOPED_TRACE(role.name);
            EXPECT_EQ(bitsOf(role.name), role.stats.toBitString()) << "migration and fresh seed disagree";
        }
        EXPECT_EQ(bitsOf("claude_test_old_gate_role"), "00000001");
        EXPECT_EQ(bitsOf("claude_test_half_gate_role"), "00000000");
        EXPECT_EQ(bitsOf("claude_test_plain_role"), "00000000");

        const auto catalog = txn.exec(
            "SELECT bit_position FROM permission WHERE name = 'admin.stats.view' AND category = 'admin'");
        ASSERT_EQ(catalog.size(), 1u);
        EXPECT_EQ(catalog.one_row()[0].as<int>(), 0);

        migseed::SqlDeployReport again;
        ASSERT_NO_THROW(again = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));
        EXPECT_TRUE(again.applied.empty());
    });
}

TEST_F(SqlDeployerHistoryDb, EveryHistoricalChecksumReconcilesToCurrent) {
    for (const auto& e : migseed::kHistoricalMigrationChecksums) {
        const std::string filename{e.filename};
        const std::string oldHash{e.sha256};
        SCOPED_TRACE(filename);

        inRolledBackTxn([&](pqxx::work& txn) {
            if (filename == "020_vaults.sql") {
                // <= 1.5.0 shape: no s3.storage_tier_id, 090 not yet applied.
                txn.exec("ALTER TABLE s3 DROP COLUMN storage_tier_id");
                txn.exec("DELETE FROM schema_migrations WHERE filename = '090_s3_storage_tier_id.sql'");
            }
            txn.exec("UPDATE schema_migrations SET sha256 = $2 WHERE filename = $1", pqxx::params{filename, oldHash});

            migseed::SqlDeployReport report;
            ASSERT_NO_THROW(report = migseed::SqlDeployer::applyDir(txn, repoPsqlDir()));
            EXPECT_TRUE(reconciled(report, filename));
            EXPECT_FALSE(applied(report, filename));
            EXPECT_EQ(recordedHash(txn, filename), currentHash(filename));

            if (filename == "020_vaults.sql") {
                EXPECT_TRUE(applied(report, "090_s3_storage_tier_id.sql"));
                EXPECT_TRUE(columnExists(txn, "s3", "storage_tier_id"));
            }
        });
    }
}

TEST_F(SqlDeployerHistoryDb, UnknownChecksumStillFailsClosed) {
    inRolledBackTxn([](pqxx::work& txn) {
        txn.exec("UPDATE schema_migrations SET sha256 = $1 WHERE filename = '060_acl.sql'",
                 pqxx::params{std::string(64, 'a')});
        EXPECT_THROW(migseed::SqlDeployer::applyDir(txn, repoPsqlDir()), std::runtime_error);
    });
}

} // namespace vh_sql_deployer_history_test
