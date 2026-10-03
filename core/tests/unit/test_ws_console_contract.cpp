// Web-console wire contract pieces that live in core: typed refusal codes on ws ERROR responses, the admin gates
// that produce them, and the cheap dashboard severity read.

#include "db/Transactions.hpp"
#include "identities/User.hpp"
#include "ops/Error.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/core/handler_templates.hpp"
#include "protocols/ws/handler/Settings.hpp"
#include "protocols/ws/handler/Stats.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "stats/model/DashboardOverview.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace vh::test_ws_console_contract {

using json = nlohmann::json;

template<class E>
protocols::ws::core::ErrorReply replyFor(const E& error) {
    try {
        throw error;
    } catch (...) {
        return protocols::ws::core::describeCurrentError();
    }
}

std::shared_ptr<protocols::ws::Session> sessionFor(const std::shared_ptr<identities::User>& user) {
    auto s = std::make_shared<protocols::ws::Session>(std::make_shared<protocols::ws::Router>());
    s->user = user;
    return s;
}

// An account whose admin role grants nothing (no DB needed for the gates under test).
std::shared_ptr<identities::User> powerlessUser() {
    auto user = std::make_shared<identities::User>();
    user->id = 4242;
    user->name = "console_contract_nobody";
    user->roles.admin = std::make_shared<rbac::role::Admin>();
    return user;
}

TEST(WsErrorCodes, OpsRefusalsCarryAStableCodeAndKeepTheirMessage) {
    auto reply = replyFor(ops::Denied("no"));
    EXPECT_EQ(reply.message, "no");
    EXPECT_EQ(reply.data, (json{{"code", "denied"}}));
    EXPECT_EQ(replyFor(ops::NotFound("gone")).data, (json{{"code", "not_found"}}));
    EXPECT_EQ(replyFor(ops::Invalid("bad")).data, (json{{"code", "invalid"}}));
    EXPECT_EQ(replyFor(ops::Conflict("taken")).data, (json{{"code", "conflict"}}));
    reply = replyFor(ops::NeedsConfirmation("encryption_waiver", "accept first"));
    EXPECT_EQ(reply.data, (json{{"code", "encryption_waiver"}}));
    EXPECT_EQ(reply.message, "accept first");

    // Faults are not refusals: message only, no data (Response omits an empty data).
    reply = replyFor(std::runtime_error("boom"));
    EXPECT_EQ(reply.message, "boom");
    EXPECT_TRUE(reply.data.empty());
    try {
        throw 7;
    } catch (...) {
        EXPECT_EQ(protocols::ws::core::describeCurrentError().message, "Unknown error");
    }
}

TEST(WsErrorCodes, AdminGatesRefuseWithDenied) {
    const auto s = sessionFor(powerlessUser());
    EXPECT_THROW((void)protocols::ws::handler::Stats::dashboardOverview(json::object(), s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Stats::dashboardSeverity(s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Stats::systemHealth(s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Stats::systemTrends(json::object(), s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Settings::get(s), ops::Denied);
}

class DashboardSeverityTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;

    static void SetUpTestSuite() {
        if (!(std::getenv("VH_TEST_DB_USER") && std::getenv("VH_TEST_DB_PASS") && std::getenv("VH_TEST_DB_HOST") &&
              std::getenv("VH_TEST_DB_PORT") && std::getenv("VH_TEST_DB_NAME"))) {
            skipTests = true;
            return;
        }
        paths::enableTestMode();
        const auto root = std::filesystem::temp_directory_path() / "vh_console_contract_severity";
        paths::backingPath = root / "backing";
        paths::mountPath = root / "mount";
        std::filesystem::create_directories(paths::backingPath);
        std::filesystem::create_directories(paths::mountPath);

        db::Transactions::init();
        db::seed::nuke_and_recreate_schema_public();
        db::Transactions::dbPool_->initPreparedStatements();
        seed::seed_database();
        runtime::Deps::init();
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
    }
};

// #146: the nav badge needs the overview's severity, not its 85 KB of cards.
TEST_F(DashboardSeverityTest, SeverityMatchesTheOverviewSummaryWithoutTheCards) {
    const auto full = stats::model::DashboardOverview::snapshot();
    const auto slim = stats::model::DashboardOverview::severity();
    EXPECT_EQ(slim.overallStatus, full.overallStatus);
    EXPECT_EQ(slim.errorCount, full.errorCount);
    EXPECT_EQ(slim.warningCount, full.warningCount);
    EXPECT_TRUE(slim.sections.empty());

    const auto payload = stats::model::dashboardSeverityJson(slim);
    EXPECT_EQ(payload.size(), 4u);
    EXPECT_TRUE(payload.at("overall_status").is_string());
    EXPECT_TRUE(payload.at("error_count").is_number_unsigned());
    EXPECT_TRUE(payload.at("warning_count").is_number_unsigned());
    EXPECT_TRUE(payload.at("checked_at").is_number_unsigned());
}

// #159: on a live (idle) test database the DB card never reports an unmeasured value as healthy, and the stats
// query's own transaction does not make oldest_tx warn.
TEST_F(DashboardSeverityTest, DbCardTonesNeverFakeHealth) {
    stats::model::DashboardOverviewRequest request;
    request.cards.push_back({.id = "system.db", .variant = "tiles", .size = "2x1"});
    const auto overview = stats::model::DashboardOverview::snapshot(request);
    ASSERT_EQ(overview.cards.size(), 1u);
    const auto& card = overview.cards.front();
    ASSERT_TRUE(card.available);

    bool sawOldestTx = false;
    bool sawSlowQueries = false;
    for (const auto& metric : card.metrics) {
        if (metric.value == "unknown") EXPECT_EQ(metric.tone, "unknown") << metric.key;
        if (metric.key == "oldest_tx") {
            sawOldestTx = true;
            EXPECT_EQ(metric.tone, "healthy") << metric.value;
        }
        if (metric.key == "slow_queries") sawSlowQueries = true;
    }
    EXPECT_TRUE(sawOldestTx);
    EXPECT_TRUE(sawSlowQueries);
}

}
