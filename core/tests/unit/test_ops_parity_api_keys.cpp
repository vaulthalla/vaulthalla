// API keys through both surfaces (Phase 2). vault::APIKeyManager used to hide its own "key belongs to the caller"
// rule under the resolver: the web passed the caller's id and refused admins the resolver allowed, the CLI passed
// the key owner's id and skipped it, and CloudEngine passed the vault owner's id, so an S3 vault using a key its
// owner may consume but does not own could not build its engine. Authorization now lives once in ops::api_keys.

#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/vault/APIKey.hpp"
#include "fs/Filesystem.hpp"
#include "identities/User.hpp"
#include "ops/APIKeys.hpp"
#include "ops/Error.hpp"
#include "ops/Roles.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/all.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/handler/vault/APIKeys.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "storage/Manager.hpp"
#include "UsageManager.hpp"
#include "vault/APIKeyManager.hpp"
#include "vault/model/APIKey.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>
#include <pqxx/pqxx>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace vh::test_ops_parity_api_keys {

using json = nlohmann::json;
using UserPtr = std::shared_ptr<identities::User>;

std::string keysTag() {
    static std::atomic<unsigned> n{0};
    static const auto run = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
    return run + "x" + std::to_string(n.fetch_add(1));
}

class APIKeyParityTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static std::shared_ptr<protocols::shell::Router> router;
    inline static UserPtr superUser, alice, bob;

    static UserPtr createUser(const std::string& name, const std::string& roleName) {
        auto user = std::make_shared<identities::User>();
        user->name = name;
        user->email = name + "@vaulthalla.test";
        user->setPasswordHash("x");
        user->roles.admin = db::query::rbac::role::Admin::get(roleName);
        user->id = db::query::identities::User::createUser(user);
        return db::query::identities::User::getUserById(user->id);
    }

    static void SetUpTestSuite() {
        if (!(std::getenv("VH_TEST_DB_USER") && std::getenv("VH_TEST_DB_PASS") && std::getenv("VH_TEST_DB_HOST") &&
              std::getenv("VH_TEST_DB_PORT") && std::getenv("VH_TEST_DB_NAME"))) {
            skipTests = true;
            std::cout << "[test_ops_parity_api_keys] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }
        paths::enableTestMode();
        const auto root = std::filesystem::temp_directory_path() / ("vh_api_key_parity_" + keysTag());
        paths::backingPath = root / "backing";
        paths::mountPath = root / "mount";
        std::filesystem::create_directories(paths::backingPath);
        std::filesystem::create_directories(paths::mountPath);

        db::Transactions::init();
        db::seed::nuke_and_recreate_schema_public();
        db::Transactions::dbPool_->initPreparedStatements();
        seed::seed_database();
        runtime::Deps::init();
        fs::Filesystem::init(runtime::Deps::get().storageManager);

        if (!runtime::Deps::get().shellUsageManager)
            runtime::Deps::get().shellUsageManager = std::make_shared<protocols::shell::UsageManager>();
        router = std::make_shared<protocols::shell::Router>();
        protocols::shell::commands::registerAPIKeyCommands(router);

        superUser = createUser("ak_super_" + keysTag(), "super_admin");
        // Users who may manage only their own keys (no built-in role is that narrow).
        const auto selfOnly = "ak_self_only_" + keysTag();
        (void)ops::roles::createAdminRole(superUser, {.name = selfOnly, .permissions = {.changes = {
            {"admin.keys.api.self.view", true}, {"admin.keys.api.self.create", true},
            {"admin.keys.api.self.remove", true}, {"admin.keys.api.self.consume", true}}}}, "test");
        alice = createUser("ak_alice_" + keysTag(), selfOnly);
        bob = createUser("ak_bob_" + keysTag(), selfOnly);
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
    }

    static int cli(const std::string& line, const UserPtr& user) {
        try {
            return router->executeLine(line, user, nullptr).exit_code;
        } catch (const std::exception&) {
            return 1;
        }
    }

    static std::shared_ptr<protocols::ws::Session> ws(const UserPtr& user) {
        auto s = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
        s->user = user;
        return s;
    }

    static unsigned int seedKey(const UserPtr& owner) {
        auto key = std::make_shared<vault::model::APIKey>(owner->id, "ak_key_" + keysTag(), vault::model::S3Provider::AWS,
                                                          "AKIATESTACCESSKEY000", "secret-value-0000000000000000000000000",
                                                          "us-east-1", "https://s3.example.com");
        return runtime::Deps::get().apiKeyManager->addAPIKey(key);
    }
};

TEST_F(APIKeyParityTest, CliAndWsCreateTheSameKeyForTheCaller) {
    const auto cliName = "ak_cli_" + keysTag(), wsName = "ak_ws_" + keysTag();
    ASSERT_EQ(cli("api-key create " + cliName + " --access AKIACLI --secret s3cr3t --provider aws --endpoint https://s3.example.com",
                  alice), 0);
    (void)protocols::ws::handler::APIKeys::add(json{{"name", wsName}, {"provider", "AWS"}, {"access_key", "AKIAWS"},
        {"secret_access_key", "s3cr3t"}, {"region", "auto"}, {"endpoint", "https://s3.example.com"}}, ws(alice));

    const auto a = db::query::vault::APIKey::getAPIKey(cliName), b = db::query::vault::APIKey::getAPIKey(wsName);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->user_id, alice->id);
    EXPECT_EQ(b->user_id, alice->id);
    EXPECT_EQ(a->provider, b->provider);
    EXPECT_EQ(a->region, b->region);
    // The secret is sealed: the stored row never carries plaintext.
    EXPECT_TRUE(a->secret_access_key.empty());
}

TEST_F(APIKeyParityTest, OwnersAndPrivilegedAdminsRemoveKeysOnBothSurfaces) {
    const auto viaCli = seedKey(alice), viaWs = seedKey(alice);
    // Someone with no rights over alice's keys is refused on both surfaces.
    EXPECT_NE(cli("api-key delete " + std::to_string(viaCli), bob), 0);
    EXPECT_THROW((void)protocols::ws::handler::APIKeys::remove(json{{"id", viaWs}}, ws(bob)), std::exception);
    EXPECT_TRUE(db::query::vault::APIKey::getAPIKey(viaCli));
    EXPECT_TRUE(db::query::vault::APIKey::getAPIKey(viaWs));

    // An admin the resolver allows succeeds on both (the web used to refuse: "does not belong to the user").
    EXPECT_EQ(cli("api-key delete " + std::to_string(viaCli), superUser), 0);
    EXPECT_NO_THROW((void)protocols::ws::handler::APIKeys::remove(json{{"id", viaWs}}, ws(superUser)));
    EXPECT_FALSE(db::query::vault::APIKey::getAPIKey(viaCli));
    EXPECT_FALSE(db::query::vault::APIKey::getAPIKey(viaWs));

    // The owner removes their own key.
    const auto own = seedKey(bob);
    EXPECT_EQ(cli("api-key delete " + std::to_string(own), bob), 0);
    EXPECT_FALSE(db::query::vault::APIKey::getAPIKey(own));
}

TEST_F(APIKeyParityTest, VisibilityMatchesAcrossSurfaces) {
    const auto alicesKey = seedKey(alice);
    EXPECT_NE(cli("api-key info " + std::to_string(alicesKey), bob), 0);
    EXPECT_THROW((void)protocols::ws::handler::APIKeys::get(json{{"id", alicesKey}}, ws(bob)), std::exception);
    EXPECT_EQ(cli("api-key info " + std::to_string(alicesKey), alice), 0);
    const auto got = protocols::ws::handler::APIKeys::get(json{{"id", alicesKey}}, ws(alice));
    EXPECT_EQ(got.at("api_key").at("api_key_id").get<unsigned int>(), alicesKey);
    EXPECT_FALSE(got.dump().contains("secret-value")) << "get must not expose the secret";

    const auto listed = json::parse(protocols::ws::handler::APIKeys::list(ws(bob)).at("keys").get<std::string>());
    for (const auto& k : listed) EXPECT_NE(k.at("user_id").get<unsigned int>(), alice->id) << "bob sees alice's key";
}

TEST_F(APIKeyParityTest, VaultUsingAConsumableKeyOfAnotherOwnerBuildsItsEngine) {
    const auto adminKey = seedKey(superUser);
    const auto vaultId = db::Transactions::exec("APIKeyParityTest::s3Vault", [&](pqxx::work& txn) {
        const auto name = "ak_vault_" + keysTag();
        const auto id = txn.exec(
            "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ('s3', $1, $2, $3, '') RETURNING id",
            pqxx::params{name, alice->id, name.substr(0, 30)}).one_field().as<unsigned int>();
        txn.exec("INSERT INTO s3 (vault_id, api_key_id, bucket) VALUES ($1, $2, 'ak-bucket')", pqxx::params{id, adminKey});
        txn.exec("WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
                 "INSERT INTO rsync (sync_id, strategy, conflict_policy) SELECT id, 'cache', 'keep_remote' FROM ins",
                 pqxx::params{id});
        return id;
    });
    EXPECT_NO_THROW(runtime::Deps::get().storageManager->initStorageEngines());
    EXPECT_NE(runtime::Deps::get().storageManager->getEngine(vaultId), nullptr)
        << "the engine used to throw 'API key does not belong to the user'";
}

}
