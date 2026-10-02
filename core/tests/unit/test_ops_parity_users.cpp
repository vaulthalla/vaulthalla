// Users through both surfaces (Phase 2). Before: the CLI wrote accounts straight to the DB (no default vault, no
// registration validation) while the web went through auth::Manager; both judged "admin" by User::isAdmin(),
// which is false for the built-in admin role, so anyone who could add users could mint or promote admins (S3);
// deleting an account through the web "invalidated" a session keyed by the user id, which matched nothing (S9);
// a deactivated account could still log in; the web could rename the super admin the daemon looks up by name;
// and auth::Manager served logins from a cache the CLI never updated. ops::users now owns all of it.

#include "auth/Manager.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/session/Manager.hpp"
#include "crypto/util/hash.hpp"
#include "db/Transactions.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/vault/Vault.hpp"
#include "fs/Filesystem.hpp"
#include "identities/User.hpp"
#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/model/Token.hpp"
#include "ops/Error.hpp"
#include "ops/Roles.hpp"
#include "ops/Users.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/all.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/handler/Auth.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "storage/Manager.hpp"
#include "UsageManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace vh::test_ops_parity_users {

using json = nlohmann::json;
using UserPtr = std::shared_ptr<identities::User>;

std::string usersTag() {
    static std::atomic<unsigned> n{0};
    static const auto run = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
    return run + "x" + std::to_string(n.fetch_add(1));
}

constexpr const char* PASSWORD = "Correct-Horse-Battery-9";

class UserParityTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static std::shared_ptr<protocols::shell::Router> router;
    inline static UserPtr superUser;

    // Seeded straight into the DB, as an existing account would be.
    static UserPtr seedUser(const std::string& prefix, const std::string& roleName) {
        auto user = std::make_shared<identities::User>();
        user->name = prefix + "_" + usersTag();
        user->email = user->name + "@vaulthalla.test";
        user->setPasswordHash(crypto::hash::password(PASSWORD));
        user->roles.admin = db::query::rbac::role::Admin::get(roleName);
        user->id = db::query::identities::User::createUser(user);
        return db::query::identities::User::getUserById(user->id);
    }

    static std::string customRole(const std::vector<std::string>& permissions) {
        const auto name = "up_role_" + usersTag();
        std::vector<std::pair<std::string, bool>> changes;
        for (const auto& p : permissions) changes.emplace_back(p, true);
        (void)ops::roles::createAdminRole(superUser, {.name = name, .permissions = {.changes = changes}}, "test");
        return name;
    }

    static void SetUpTestSuite() {
        if (!(std::getenv("VH_TEST_DB_USER") && std::getenv("VH_TEST_DB_PASS") && std::getenv("VH_TEST_DB_HOST") &&
              std::getenv("VH_TEST_DB_PORT") && std::getenv("VH_TEST_DB_NAME"))) {
            skipTests = true;
            std::cout << "[test_ops_parity_users] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }
        paths::enableTestMode();
        const auto root = std::filesystem::temp_directory_path() / ("vh_user_parity_" + usersTag());
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
        auth::session::Issuer::setJwtSecretForTesting("user-parity-test-secret");

        if (!runtime::Deps::get().shellUsageManager)
            runtime::Deps::get().shellUsageManager = std::make_shared<protocols::shell::UsageManager>();
        router = std::make_shared<protocols::shell::Router>();
        protocols::shell::commands::registerUserCommands(router);

        superUser = seedUser("up_super", "super_admin");
    }

    static void TearDownTestSuite() {
        if (!skipTests) auth::session::Issuer::clearJwtSecretForTesting();
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
    }

    static std::pair<int, std::string> cli(const std::string& line, const UserPtr& user) {
        try {
            const auto res = router->executeLine(line, user, nullptr);
            return {res.exit_code, res.stdout_text + res.stderr_text};
        } catch (const std::exception& e) {
            return {1, e.what()};
        }
    }

    static std::shared_ptr<protocols::ws::Session> ws(const UserPtr& user) {
        auto s = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
        s->ipAddress = "127.0.0.1";
        s->userAgent = "user-parity-test";
        s->user = user;
        return s;
    }

    static bool wsOk(const std::function<void()>& fn) {
        try {
            fn();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    static std::string roleOf(const unsigned int id) {
        const auto u = db::query::identities::User::getUserById(id);
        return u && u->roles.admin ? u->roles.admin->name : "";
    }

    // A socket as the server accepts it: anonymous, with a refresh token, then logged in.
    static std::shared_ptr<protocols::ws::Session> connected() {
        auto session = ws(nullptr);
        runtime::Deps::get().sessionManager->rotateRefreshToken(session);
        return session;
    }

    static std::shared_ptr<protocols::ws::Session> loggedIn(const UserPtr& user) { return loggedIn(user->name); }

    static std::shared_ptr<protocols::ws::Session> loggedIn(const std::string& name) {
        auto session = connected();
        runtime::Deps::get().authManager->loginUser(name, PASSWORD, session);
        return session;
    }

    static bool hasLiveRefreshToken(const unsigned int userId) {
        for (const auto& t : db::query::auth::RefreshToken::list(userId)) if (t && !t->revoked) return true;
        return false;
    }
};

TEST_F(UserParityTest, CreateMakesTheSameAccountAndDefaultVaultOnBothSurfaces) {
    const auto cliName = "up_cli_" + usersTag(), wsName = "up_ws_" + usersTag();
    const auto [code, out] = cli("user create " + cliName + " --role unprivileged --email " + cliName + "@x.test", superUser);
    ASSERT_EQ(code, 0) << out;
    EXPECT_NE(out.find("Password: "), std::string::npos) << "the generated password is shown once";
    (void)protocols::ws::handler::Auth::registerUser(json{{"name", wsName}, {"email", wsName + "@x.test"},
        {"password", PASSWORD}, {"is_active", true}, {"role", "unprivileged"}}, ws(superUser));

    const auto a = db::query::identities::User::getUserByName(cliName), b = db::query::identities::User::getUserByName(wsName);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->roles.admin->name, b->roles.admin->name);
    EXPECT_EQ(a->meta.is_active, b->meta.is_active);
    EXPECT_FALSE(db::query::vault::Vault::listUserVaults(a->id).empty()) << "the CLI used to skip the default vault";
    EXPECT_EQ(db::query::vault::Vault::listUserVaults(a->id).size(), db::query::vault::Vault::listUserVaults(b->id).size());

    // Validation is the same too: a bad email is refused on both.
    EXPECT_NE(cli("user create up_bad_" + usersTag() + " --role unprivileged --email nope", superUser).first, 0);
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::registerUser(json{{"name", "up_bad_" + usersTag()}, {"email", "nope"},
            {"password", PASSWORD}, {"role", "unprivileged"}}, ws(superUser));
    }));
}

TEST_F(UserParityTest, BuiltInRolesClassifyAsAdminsExceptUnprivileged) {
    for (const auto& role : db::query::rbac::role::Admin::list()) {
        if (role->name.starts_with("up_")) continue;
        EXPECT_EQ(ops::users::isAdminIdentity(*role), role->name != "unprivileged") << role->name;
    }
}

TEST_F(UserParityTest, NobodyMintsOrPromotesAccountsAboveThemselves) {
    // identity_admin may add and edit users, but holds far less than the built-in admin role (S3).
    const auto idAdmin = seedUser("up_idadmin", "identity_admin");
    EXPECT_NE(cli("user create up_mint_" + usersTag() + " --role admin", idAdmin).first, 0);
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::registerUser(json{{"name", "up_mint_" + usersTag()},
            {"password", PASSWORD}, {"role", "admin"}}, ws(idAdmin));
    }));

    const auto plain = seedUser("up_plain", "unprivileged");
    EXPECT_NE(cli("user update " + plain->name + " --role admin", idAdmin).first, 0);
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::updateUser(json{{"id", plain->id}, {"role", "admin"}}, ws(idAdmin));
    }));
    EXPECT_EQ(roleOf(plain->id), "unprivileged");

    // Ordinary account management still works for them.
    const auto [code, out] = cli("user create up_ok_" + usersTag() + " --role unprivileged", idAdmin);
    EXPECT_EQ(code, 0) << out;
}

TEST_F(UserParityTest, AccountsAboveTheActorCannotBeManaged) {
    // Holds every identity permission, nothing else: can manage admin-class accounts in general, but not one whose
    // role grants what it lacks (an auditor sees audits) - no reset-and-take-over, no delete.
    const auto mgr = seedUser("up_mgr", customRole({
        "admin.identities.admins.view", "admin.identities.admins.add", "admin.identities.admins.edit",
        "admin.identities.admins.delete", "admin.identities.admins.reset-password",
        "admin.identities.users.view", "admin.identities.users.add", "admin.identities.users.edit",
        "admin.identities.users.delete", "admin.identities.users.reset-password"}));
    const auto auditor = seedUser("up_auditor", "auditor");

    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::changePassword(json{{"id", auditor->id}, {"new_password", "Another-Pass-77"}}, ws(mgr));
    }));
    EXPECT_NE(cli("user delete " + auditor->name, mgr).first, 0);
    EXPECT_FALSE(wsOk([&] { (void)protocols::ws::handler::Auth::deleteUser(json{{"id", auditor->id}}, ws(mgr)); }));
    EXPECT_NE(cli("user update " + auditor->name + " --disable", mgr).first, 0);
    EXPECT_TRUE(db::query::identities::User::getUserById(auditor->id));

    // An admin-class account within the actor's reach is fine.
    const auto minor = seedUser("up_minor", customRole({"admin.identities.users.view"}));
    EXPECT_EQ(cli("user update " + minor->name + " --email minor@x.test", mgr).first, 0);
    EXPECT_TRUE(wsOk([&] { (void)protocols::ws::handler::Auth::deleteUser(json{{"id", minor->id}}, ws(mgr)); }));
}

TEST_F(UserParityTest, DeactivationAndDeletionEndSessions) {
    // S9: deleting through the web "invalidated" a session keyed by the user id, which matched nothing.
    const auto& sessions = runtime::Deps::get().sessionManager;
    const auto victim = seedUser("up_victim", "unprivileged");
    const auto session = loggedIn(victim);
    const auto accessToken = session->tokens->accessToken->rawToken;
    ASSERT_FALSE(sessions->getSessionsByUserId(victim->id).empty());
    ASSERT_TRUE(hasLiveRefreshToken(victim->id));

    ASSERT_EQ(cli("user update " + victim->name + " --disable", superUser).first, 0);
    EXPECT_TRUE(sessions->getSessionsByUserId(victim->id).empty());
    EXPECT_FALSE(hasLiveRefreshToken(victim->id));
    EXPECT_FALSE(sessions->validate(session, accessToken)) << "the open socket kept working";
    EXPECT_THROW(loggedIn(victim), std::exception) << "deactivated accounts used to log in";

    // Deleted through the web: same outcome.
    const auto gone = seedUser("up_gone", "unprivileged");
    const auto goneSession = loggedIn(gone);
    const auto goneToken = goneSession->tokens->accessToken->rawToken;
    (void)protocols::ws::handler::Auth::deleteUser(json{{"id", gone->id}}, ws(superUser));
    EXPECT_TRUE(sessions->getSessionsByUserId(gone->id).empty());
    EXPECT_FALSE(sessions->validate(goneSession, goneToken));

    // A role change ends sessions too: they carry the old role.
    const auto demoted = seedUser("up_demoted", "auditor");
    (void)loggedIn(demoted);
    ASSERT_EQ(cli("user update " + demoted->name + " --role unprivileged", superUser).first, 0);
    EXPECT_TRUE(sessions->getSessionsByUserId(demoted->id).empty());
}

TEST_F(UserParityTest, LoginReadsTheAccountAsItIsNow) {
    // A CLI rename used to leave the old name logging in from auth::Manager's cache.
    const auto user = seedUser("up_renamed", "unprivileged");
    (void)loggedIn(user);
    const auto newName = "up_newname_" + usersTag();
    ASSERT_EQ(cli("user update " + user->name + " --name " + newName, superUser).first, 0);
    EXPECT_THROW((void)loggedIn(user->name), std::exception);
    EXPECT_NO_THROW((void)loggedIn(newName));
}

TEST_F(UserParityTest, SelfServiceLimitsMatch) {
    // The daemon finds the super admin by name: the web used to let it rename itself.
    EXPECT_NE(cli("user update " + superUser->name + " --name up_renamed_super", superUser).first, 0);
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::updateUser(json{{"id", superUser->id}, {"name", "up_renamed_super"}}, ws(superUser));
    }));

    const auto self = seedUser("up_self", "identity_admin");
    EXPECT_NE(cli("user delete " + self->name, self).first, 0);
    EXPECT_FALSE(wsOk([&] { (void)protocols::ws::handler::Auth::deleteUser(json{{"id", self->id}}, ws(self)); }));
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::updateUser(json{{"id", self->id}, {"is_active", false}}, ws(self));
    }));
    EXPECT_NE(cli("user update " + self->name + " --role unprivileged", self).first, 0);

    // The web never validated email on update.
    EXPECT_FALSE(wsOk([&] {
        (void)protocols::ws::handler::Auth::updateUser(json{{"id", self->id}, {"email", "not-an-email"}}, ws(self));
    }));
    EXPECT_NE(cli("user update " + self->name + " --email not-an-email", self).first, 0);
    EXPECT_TRUE(wsOk([&] {
        (void)protocols::ws::handler::Auth::updateUser(json{{"id", self->id}, {"email", "fine@x.test"}}, ws(self));
    }));
}

TEST_F(UserParityTest, ListShowsWhatTheActorMayViewOnBothSurfaces) {
    const auto viewer = seedUser("up_viewer", customRole({"admin.identities.users.view"}));
    (void)seedUser("up_listed_plain", "unprivileged");
    (void)seedUser("up_listed_admin", "auditor");

    const auto listed = protocols::ws::handler::Auth::listUsers(ws(viewer)).at("users");
    std::set<std::string> wsNames;
    for (const auto& u : listed) {
        wsNames.insert(u.at("name").get<std::string>());
        const auto acct = db::query::identities::User::getUserById(u.at("id").get<unsigned int>());
        if (acct->id != viewer->id) EXPECT_FALSE(ops::users::isAdminIdentity(*acct->roles.admin)) << acct->name;
    }
    EXPECT_TRUE(wsNames.contains(viewer->name));

    const auto [code, out] = cli("user list", viewer);
    ASSERT_EQ(code, 0) << out;
    for (const auto& name : wsNames) EXPECT_NE(out.find(name), std::string::npos) << name;

    // Nobody without a view permission lists anything.
    EXPECT_NE(cli("user list", seedUser("up_blind", "unprivileged")).first, 0);
    // And --sort is a column, not SQL.
    EXPECT_NE(cli("user list --sort \"id; DELETE FROM users\"", superUser).first, 0);
    EXPECT_TRUE(db::query::identities::User::getUserById(viewer->id));
}

}
