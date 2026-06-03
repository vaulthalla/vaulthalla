#include "auth/Manager.hpp"
#include "crypto/util/hash.hpp"
#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "identities/User.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/handler/Auth.hpp"
#include "rbac/permission/admin/Identities.hpp"
#include "rbac/permission/admin/identities/Base.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace vh::auth::test_password_change {
using json = nlohmann::json;

struct ScopedRuntimeAuthManager {
    std::shared_ptr<vh::auth::Manager> previous;

    explicit ScopedRuntimeAuthManager(std::shared_ptr<vh::auth::Manager> manager)
        : previous(vh::runtime::Deps::get().authManager) {
        vh::runtime::Deps::get().authManager = std::move(manager);
    }

    ~ScopedRuntimeAuthManager() {
        vh::runtime::Deps::get().authManager = std::move(previous);
    }
};

std::shared_ptr<vh::protocols::ws::Session> sessionFor(const std::shared_ptr<vh::identities::User>& user) {
    auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());
    session->user = user;
    return session;
}

bool authenticate(const std::string& name, const std::string& password) {
    return vh::db::query::identities::User::authenticateUser(name, password);
}

class AuthPasswordChangeTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;

    static bool hasDbEnv() {
        return std::getenv("VH_TEST_DB_USER") &&
               std::getenv("VH_TEST_DB_PASS") &&
               std::getenv("VH_TEST_DB_HOST") &&
               std::getenv("VH_TEST_DB_PORT") &&
               std::getenv("VH_TEST_DB_NAME");
    }

    static void SetUpTestSuite() {
        if (!hasDbEnv()) {
            skipTests = true;
            std::cout << "[test_auth_password_change] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }

        vh::paths::enableTestMode();
        vh::db::Transactions::init();
        vh::db::seed::nuke_and_recreate_schema_public();
        vh::db::seed::init_tables_if_not_exists();
        vh::db::Transactions::dbPool_->initPreparedStatements();
        vh::seed::initPermissions();
        vh::seed::initRoles();
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
    }

    static std::shared_ptr<vh::identities::User> createUser(
        const std::string& name,
        const std::string& password,
        const std::string& roleName = "unprivileged"
    ) {
        auto user = std::make_shared<vh::identities::User>();
        user->name = name;
        user->email = name + "@vaulthalla.test";
        user->setPasswordHash(vh::crypto::hash::password(password));
        user->roles.admin = vh::db::query::rbac::role::Admin::get(roleName);
        if (!user->roles.admin) throw std::runtime_error("Missing admin role: " + roleName);

        user->id = vh::db::query::identities::User::createUser(user);
        return vh::db::query::identities::User::getUserById(user->id);
    }

    static std::shared_ptr<vh::identities::User> createSystemOnlyUser(
        const std::string& name,
        const std::string& password
    ) {
        const auto role = vh::db::query::rbac::role::Admin::get("unprivileged");
        if (!role) throw std::runtime_error("Missing unprivileged role");

        const auto id = vh::db::Transactions::exec("AuthPasswordChangeTest::createSystemOnlyUser", [&](pqxx::work& txn) {
            txn.exec("SELECT set_config('vaulthalla.bootstrap', 'on', true)");

            const auto userId = txn.exec(
                R"SQL(
                    INSERT INTO users (name, email, password_hash, is_active, protected, system_only)
                    VALUES ($1, $2, $3, TRUE, FALSE, TRUE)
                    RETURNING id
                )SQL",
                pqxx::params{name, name + "@vaulthalla.test", vh::crypto::hash::password(password)}
            ).one_field().as<unsigned int>();

            txn.exec(
                "INSERT INTO admin_role_assignments (user_id, role_id) VALUES ($1, $2)",
                pqxx::params{userId, role->id}
            );

            return userId;
        });

        return vh::db::query::identities::User::getUserById(id);
    }
};
TEST(AuthPasswordPermissionTest, ResetPasswordBitIsAdditiveAndSeededOnlyForOrgAndSuperAdmins) {
    using P = vh::rbac::permission::admin::identities::IdentityPermissions;

    EXPECT_EQ(static_cast<unsigned int>(P::View), 1u);
    EXPECT_EQ(static_cast<unsigned int>(P::Add), 2u);
    EXPECT_EQ(static_cast<unsigned int>(P::Edit), 4u);
    EXPECT_EQ(static_cast<unsigned int>(P::Delete), 8u);
    EXPECT_EQ(static_cast<unsigned int>(P::ResetPassword), 16u);

    const auto orgAdmin = vh::rbac::role::Admin::OrgAdmin();
    EXPECT_TRUE(orgAdmin.identities.users.canResetPassword());
    EXPECT_TRUE(orgAdmin.identities.admins.canResetPassword());

    const auto superAdmin = vh::rbac::role::Admin::SuperAdmin();
    EXPECT_TRUE(superAdmin.identities.users.canResetPassword());
    EXPECT_TRUE(superAdmin.identities.admins.canResetPassword());

    const auto identityAdmin = vh::rbac::role::Admin::IdentityAdmin();
    EXPECT_FALSE(identityAdmin.identities.users.canResetPassword());
    EXPECT_FALSE(identityAdmin.identities.admins.canResetPassword());

    const auto securityAdmin = vh::rbac::role::Admin::SecurityAdmin();
    EXPECT_FALSE(securityAdmin.identities.users.canResetPassword());
    EXPECT_FALSE(securityAdmin.identities.admins.canResetPassword());
}

TEST_F(AuthPasswordChangeTest, SelfPasswordChangePersistsToDatabase) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto user = createUser("self_password_change", "old-password");
    const auto session = sessionFor(user);

    const auto response = vh::protocols::ws::handler::Auth::changePassword(
        json{{"id", user->id}, {"old_password", "old-password"}, {"new_password", "new-password"}},
        session
    );

    EXPECT_EQ(response.at("user").at("id").get<unsigned int>(), user->id);
    EXPECT_FALSE(authenticate(user->name, "old-password"));
    EXPECT_TRUE(authenticate(user->name, "new-password"));
}

TEST_F(AuthPasswordChangeTest, SelfPasswordChangeRequiresOldPassword) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto user = createUser("self_password_missing_old", "old-password");
    const auto session = sessionFor(user);

    EXPECT_THROW(
        (void) vh::protocols::ws::handler::Auth::changePassword(
            json{{"id", user->id}, {"new_password", "new-password"}},
            session
        ),
        std::runtime_error
    );

    EXPECT_TRUE(authenticate(user->name, "old-password"));
    EXPECT_FALSE(authenticate(user->name, "new-password"));
}

TEST_F(AuthPasswordChangeTest, AdminResetPersistsWithoutOldPasswordWhenPermissionIsGranted) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto actor = createUser("password_reset_actor", "actor-password", "admin");
    const auto target = createUser("password_reset_target", "old-password");
    const auto session = sessionFor(actor);

    const auto response = vh::protocols::ws::handler::Auth::changePassword(
        json{{"id", target->id}, {"new_password", "new-password"}},
        session
    );

    EXPECT_EQ(response.at("user").at("id").get<unsigned int>(), target->id);
    EXPECT_FALSE(authenticate(target->name, "old-password"));
    EXPECT_TRUE(authenticate(target->name, "new-password"));
}

TEST_F(AuthPasswordChangeTest, AdminResetIsDeniedWithoutResetPermission) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto actor = createUser("password_reset_denied_actor", "actor-password", "identity_admin");
    const auto target = createUser("password_reset_denied_target", "old-password");
    const auto session = sessionFor(actor);

    EXPECT_THROW(
        (void) vh::protocols::ws::handler::Auth::changePassword(
            json{{"id", target->id}, {"new_password", "new-password"}},
            session
        ),
        std::runtime_error
    );

    EXPECT_TRUE(authenticate(target->name, "old-password"));
    EXPECT_FALSE(authenticate(target->name, "new-password"));
}

TEST_F(AuthPasswordChangeTest, AdminResetUsesAdminScopeForAdminTargets) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto actor = createUser("admin_password_reset_actor", "actor-password", "admin");
    const auto target = createUser("admin_password_reset_target", "old-password", "super_admin");
    const auto session = sessionFor(actor);

    const auto response = vh::protocols::ws::handler::Auth::changePassword(
        json{{"id", target->id}, {"new_password", "new-password"}},
        session
    );

    EXPECT_EQ(response.at("user").at("id").get<unsigned int>(), target->id);
    EXPECT_FALSE(authenticate(target->name, "old-password"));
    EXPECT_TRUE(authenticate(target->name, "new-password"));
}

TEST_F(AuthPasswordChangeTest, PasswordResetRejectsSystemOnlyTarget) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto actor = createUser("system_only_reset_actor", "actor-password", "admin");
    const auto target = createSystemOnlyUser("system_only_reset_target", "old-password");
    const auto session = sessionFor(actor);

    EXPECT_THROW(
        (void) vh::protocols::ws::handler::Auth::changePassword(
            json{{"id", target->id}, {"new_password", "new-password"}},
            session
        ),
        std::runtime_error
    );
}

TEST_F(AuthPasswordChangeTest, ChangePasswordRejectsMissingSessionUser) {
    auto manager = std::make_shared<vh::auth::Manager>();
    ScopedRuntimeAuthManager scoped(manager);

    const auto target = createUser("missing_session_target", "old-password");
    const auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());

    EXPECT_THROW(
        (void) vh::protocols::ws::handler::Auth::changePassword(
            json{{"id", target->id}, {"new_password", "new-password"}},
            session
        ),
        std::runtime_error
    );

    EXPECT_TRUE(authenticate(target->name, "old-password"));
}

} // namespace vh::auth::test_password_change
