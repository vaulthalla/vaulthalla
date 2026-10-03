// Web-console wire contract pieces that live in core: typed refusal codes on ws ERROR responses and the admin gates
// that produce them.

#include "identities/User.hpp"
#include "ops/Error.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/core/handler_templates.hpp"
#include "protocols/ws/handler/Settings.hpp"
#include "protocols/ws/handler/Stats.hpp"
#include "rbac/role/Admin.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
    EXPECT_THROW((void)protocols::ws::handler::Stats::systemHealth(s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Stats::systemTrends(json::object(), s), ops::Denied);
    EXPECT_THROW((void)protocols::ws::handler::Settings::get(s), ops::Denied);
}
}
