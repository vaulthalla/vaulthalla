#include "rbac/role/Vault.hpp"
#include "rbac/role/Admin.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/Transactions.hpp"
#include "seed/include/init_db_tables.hpp"

#include <gtest/gtest.h>
#include <iostream>
#include <paths.h>

using namespace vh;

class PermExportTest : public ::testing::Test {
protected:
    void SetUp() override {
        vh::paths::enableTestMode();
        vh::db::Transactions::init();
        vh::db::seed::init_tables_if_not_exists();
        vh::db::Transactions::dbPool_->initPreparedStatements();
    }
};

TEST_F(PermExportTest, TestVaultPermExport) {
    const auto vRole = rbac::role::Vault::PowerUser();
    EXPECT_FALSE(vRole.toFlagsString().empty());
    // std::cout << vRole.toFlagsString() << std::endl;

    const auto flags = vRole.getFlags();
    EXPECT_FALSE(flags.empty());
    // for (auto& perm : flags)
    //     std::cout << perm << std::endl;
}

TEST_F(PermExportTest, TestAdminPermExport) {
    const auto role = rbac::role::Admin::SecurityAdmin();
    EXPECT_FALSE(role.toFlagsString().empty());

    const auto flags = role.getFlags();
    EXPECT_FALSE(flags.empty());
    // for (auto& perm : flags)
    //     std::cout << perm << std::endl;
}

TEST_F(PermExportTest, TestToBitString) {
    const auto role = rbac::role::Admin::SuperAdmin();
    const auto& s = role.settings;

    std::cout << "\n=== SuperAdmin Settings ===\n";
    std::cout << "BitString: " << s.toBitString() << '\n';
    std::cout << "Mask: " << s.toMask() << '\n';

    std::cout << "websocket raw: " << +s.websocket.raw() << '\n';
    std::cout << "http raw: " << +s.http.raw() << '\n';
    std::cout << "database raw: " << +s.database.raw() << '\n';
    std::cout << "auth raw: " << +s.auth.raw() << '\n';
    std::cout << "logging raw: " << +s.logging.raw() << '\n';
    std::cout << "caching raw: " << +s.caching.raw() << '\n';
    std::cout << "sharing raw: " << +s.sharing.raw() << '\n';
    std::cout << "services raw: " << +s.services.raw() << '\n';

    rbac::permission::admin::Settings roundtrip;
    roundtrip.fromMask(s.toMask());

    std::cout << "\n=== Roundtrip ===\n";
    std::cout << "BitString: " << roundtrip.toBitString() << '\n';
    std::cout << "Mask: " << roundtrip.toMask() << '\n';

    std::cout << "websocket rt raw: " << +roundtrip.websocket.raw() << '\n';
    std::cout << "http rt raw: " << +roundtrip.http.raw() << '\n';
    std::cout << "database rt raw: " << +roundtrip.database.raw() << '\n';
    std::cout << "auth rt raw: " << +roundtrip.auth.raw() << '\n';
    std::cout << "logging rt raw: " << +roundtrip.logging.raw() << '\n';
    std::cout << "caching rt raw: " << +roundtrip.caching.raw() << '\n';
    std::cout << "sharing rt raw: " << +roundtrip.sharing.raw() << '\n';
    std::cout << "services rt raw: " << +roundtrip.services.raw() << '\n';

    EXPECT_EQ(roundtrip.toMask(), s.toMask());
    EXPECT_EQ(roundtrip.websocket.raw(), s.websocket.raw());
    EXPECT_EQ(roundtrip.http.raw(), s.http.raw());
    EXPECT_EQ(roundtrip.database.raw(), s.database.raw());
    EXPECT_EQ(roundtrip.auth.raw(), s.auth.raw());
    EXPECT_EQ(roundtrip.logging.raw(), s.logging.raw());
    EXPECT_EQ(roundtrip.caching.raw(), s.caching.raw());
    EXPECT_EQ(roundtrip.sharing.raw(), s.sharing.raw());
    EXPECT_EQ(roundtrip.services.raw(), s.services.raw());

    const auto exported = s.exportPermissions();
    std::size_t enabledCount = 0;

    std::cout << "\n=== Exported Permissions ===\n";
    for (const auto& p : exported.permissions) {
        if (p.value) ++enabledCount;
        std::cout
            << p.qualified_name
            << " | bit=" << p.bit_position
            << " | raw=" << p.rawValue
            << " | value=" << std::boolalpha << (p.value ? "true" : "false")
            << '\n';
    }

    std::cout << "Enabled exported count: " << enabledCount << " / " << exported.permissions.size() << '\n';
}

TEST_F(PermExportTest, TestSettingsPackRoundTrip) {
    rbac::permission::admin::Settings s = rbac::permission::admin::Settings::None();
    s.websocket = rbac::permission::admin::settings::Websocket::View(); // 1
    s.http = rbac::permission::admin::settings::Http::Edit();           // 3
    s.database = rbac::permission::admin::settings::Database::View();   // 1
    s.auth = rbac::permission::admin::settings::Auth::Edit();           // 3
    s.logging = rbac::permission::admin::settings::Logging::View();     // 1
    s.caching = rbac::permission::admin::settings::Caching::Edit();     // 3
    s.sharing = rbac::permission::admin::settings::Sharing::View();     // 1
    s.services = rbac::permission::admin::settings::Services::Edit();   // 3

    std::cout << "BitString: " << s.toBitString() << '\n';
    std::cout << "Mask: " << s.toMask() << '\n';

    std::cout << "websocket raw: " << +s.websocket.raw() << '\n';
    std::cout << "http raw: " << +s.http.raw() << '\n';
    std::cout << "database raw: " << +s.database.raw() << '\n';
    std::cout << "auth raw: " << +s.auth.raw() << '\n';
    std::cout << "logging raw: " << +s.logging.raw() << '\n';
    std::cout << "caching raw: " << +s.caching.raw() << '\n';
    std::cout << "sharing raw: " << +s.sharing.raw() << '\n';
    std::cout << "services raw: " << +s.services.raw() << '\n';

    rbac::permission::admin::Settings rt;
    rt.fromMask(s.toMask());

    EXPECT_EQ(rt.websocket.raw(), s.websocket.raw());
    EXPECT_EQ(rt.http.raw(), s.http.raw());
    EXPECT_EQ(rt.database.raw(), s.database.raw());
    EXPECT_EQ(rt.auth.raw(), s.auth.raw());
    EXPECT_EQ(rt.logging.raw(), s.logging.raw());
    EXPECT_EQ(rt.caching.raw(), s.caching.raw());
    EXPECT_EQ(rt.sharing.raw(), s.sharing.raw());
    EXPECT_EQ(rt.services.raw(), s.services.raw());
}

TEST_F(PermExportTest, TestSuperAdminDBRetrieval) {
    const auto r = std::make_shared<rbac::role::Admin>(rbac::role::Admin::SuperAdmin());
    r->id = 0;
    r->name = "test_super_admin";
    r->description = "A test super admin role";

    const auto initial = r->settings.toBitString();
    db::query::rbac::role::Admin::upsert(r);

    const auto rRoundtrip = db::query::rbac::role::Admin::get(r->name);
    const auto after = rRoundtrip->settings.toBitString();

    std::cout << "Initial: " << initial << '\n';
    std::cout << "Roundtrip: " << after << std::endl;

    EXPECT_EQ(after, initial);
}
