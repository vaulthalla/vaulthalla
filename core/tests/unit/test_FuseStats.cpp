#include "runtime/Deps.hpp"
#include "stats/model/DashboardOverview.hpp"
#include "stats/model/FuseStats.hpp"

#include <cerrno>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using vh::stats::model::DashboardCardSummary;
using vh::stats::model::DashboardOverview;
using vh::stats::model::DashboardOverviewRequest;
using vh::stats::model::FuseOperation;
using vh::stats::model::FuseOpStatsSnapshot;
using vh::stats::model::FuseStats;

const FuseOpStatsSnapshot* findOp(const vh::stats::model::FuseStatsSnapshot& snapshot, const std::string& name) {
    for (const auto& op : snapshot.ops) {
        if (op.op == name) return &op;
    }
    return nullptr;
}

const DashboardCardSummary* findCard(const DashboardOverview& overview, const std::string& id) {
    for (const auto& card : overview.cards) {
        if (card.id == id) return &card;
    }
    return nullptr;
}

std::optional<double> metricValue(const DashboardCardSummary& card, const std::string& key) {
    for (const auto& metric : card.metrics) {
        if (metric.key == key) return metric.numericValue;
    }
    return std::nullopt;
}

class FuseStatsDepsGuard {
public:
    explicit FuseStatsDepsGuard(std::shared_ptr<FuseStats> stats)
        : previous_(vh::runtime::Deps::get().fuseStats) {
        vh::runtime::Deps::get().fuseStats = std::move(stats);
    }

    ~FuseStatsDepsGuard() {
        vh::runtime::Deps::get().fuseStats = std::move(previous_);
    }

private:
    std::shared_ptr<FuseStats> previous_;
};

DashboardOverviewRequest fuseCardRequest() {
    DashboardOverviewRequest request;
    request.cards.push_back({.id = "system.fuse", .variant = "tiles", .size = "2x1"});
    return request;
}

}

TEST(FuseStatsTest, LookupEnoentIsExpectedButNotAlertable) {
    FuseStats stats;
    stats.record_success(FuseOperation::GetAttr, 100);
    stats.record_error(FuseOperation::Lookup, ENOENT, 250);

    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.totalOps, 2u);
    EXPECT_EQ(snapshot.totalSuccesses, 1u);
    EXPECT_EQ(snapshot.totalErrors, 1u);
    EXPECT_EQ(snapshot.expectedErrors, 1u);
    EXPECT_EQ(snapshot.alertableErrors, 0u);
    EXPECT_DOUBLE_EQ(snapshot.errorRate, 0.5);
    EXPECT_DOUBLE_EQ(snapshot.expectedErrorRate, 0.5);
    EXPECT_DOUBLE_EQ(snapshot.alertableErrorRate, 0.0);

    const auto* lookup = findOp(snapshot, "lookup");
    ASSERT_NE(lookup, nullptr);
    EXPECT_EQ(lookup->count, 1u);
    EXPECT_EQ(lookup->errors, 1u);
    EXPECT_EQ(lookup->expectedErrors, 1u);
    EXPECT_EQ(lookup->alertableErrors, 0u);
    EXPECT_DOUBLE_EQ(lookup->errorRate, 1.0);
    EXPECT_DOUBLE_EQ(lookup->expectedErrorRate, 1.0);
    EXPECT_DOUBLE_EQ(lookup->alertableErrorRate, 0.0);
}

TEST(FuseStatsTest, NonLookupAndPermissionErrorsAreAlertable) {
    FuseStats stats;
    stats.record_error(FuseOperation::Lookup, ENOENT, 10);
    stats.record_error(FuseOperation::Read, EIO, 20);
    stats.record_error(FuseOperation::Open, EACCES, 30);
    stats.record_error(FuseOperation::Write, EBADF, 40);
    stats.record_success(FuseOperation::ReadDir, 50);
    stats.record_success(FuseOperation::StatFs, 60);

    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.totalOps, 6u);
    EXPECT_EQ(snapshot.totalErrors, 4u);
    EXPECT_EQ(snapshot.expectedErrors, 1u);
    EXPECT_EQ(snapshot.alertableErrors, 3u);
    EXPECT_DOUBLE_EQ(snapshot.errorRate, 4.0 / 6.0);
    EXPECT_DOUBLE_EQ(snapshot.expectedErrorRate, 1.0 / 6.0);
    EXPECT_DOUBLE_EQ(snapshot.alertableErrorRate, 3.0 / 6.0);

    const auto* open = findOp(snapshot, "open");
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->expectedErrors, 0u);
    EXPECT_EQ(open->alertableErrors, 1u);
    EXPECT_DOUBLE_EQ(open->alertableErrorRate, 1.0);
}

TEST(FuseStatsTest, UnlinkEnoentRemainsAlertable) {
    FuseStats stats;
    stats.record_error(FuseOperation::Unlink, ENOENT, 10);

    const auto snapshot = stats.snapshot();
    EXPECT_EQ(snapshot.totalOps, 1u);
    EXPECT_EQ(snapshot.expectedErrors, 0u);
    EXPECT_EQ(snapshot.alertableErrors, 1u);

    const auto* unlink = findOp(snapshot, "unlink");
    ASSERT_NE(unlink, nullptr);
    EXPECT_EQ(unlink->errors, 1u);
    EXPECT_EQ(unlink->expectedErrors, 0u);
    EXPECT_EQ(unlink->alertableErrors, 1u);
}

TEST(FuseStatsTest, DashboardIgnoresExpectedLookupMissesForFuseSeverity) {
    auto stats = std::make_shared<FuseStats>();
    for (int i = 0; i < 100; ++i) stats->record_error(FuseOperation::Lookup, ENOENT, 10);
    FuseStatsDepsGuard guard(stats);

    const auto overview = DashboardOverview::snapshot(fuseCardRequest());
    const auto* card = findCard(overview, "system.fuse");
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->severity, "info");
    EXPECT_TRUE(card->warnings.empty());
    EXPECT_TRUE(card->errors.empty());
    ASSERT_TRUE(metricValue(*card, "error_rate"));
    ASSERT_TRUE(metricValue(*card, "alertable_error_rate"));
    EXPECT_DOUBLE_EQ(*metricValue(*card, "error_rate"), 1.0);
    EXPECT_DOUBLE_EQ(*metricValue(*card, "alertable_error_rate"), 0.0);
}

TEST(FuseStatsTest, DashboardWarnsAndErrorsOnAlertableFuseErrors) {
    auto warningStats = std::make_shared<FuseStats>();
    for (int i = 0; i < 97; ++i) warningStats->record_success(FuseOperation::Lookup, 10);
    for (int i = 0; i < 3; ++i) warningStats->record_error(FuseOperation::Read, EIO, 10);
    FuseStatsDepsGuard warningGuard(warningStats);

    const auto warningOverview = DashboardOverview::snapshot(fuseCardRequest());
    const auto* warningCard = findCard(warningOverview, "system.fuse");
    ASSERT_NE(warningCard, nullptr);
    EXPECT_EQ(warningCard->severity, "warning");
    EXPECT_EQ(warningCard->warnings.size(), 1u);
    EXPECT_TRUE(warningCard->errors.empty());
    ASSERT_TRUE(metricValue(*warningCard, "alertable_error_rate"));
    EXPECT_DOUBLE_EQ(*metricValue(*warningCard, "alertable_error_rate"), 0.03);

    auto errorStats = std::make_shared<FuseStats>();
    for (int i = 0; i < 89; ++i) errorStats->record_success(FuseOperation::Lookup, 10);
    for (int i = 0; i < 11; ++i) errorStats->record_error(FuseOperation::Write, EIO, 10);
    vh::runtime::Deps::get().fuseStats = errorStats;

    const auto errorOverview = DashboardOverview::snapshot(fuseCardRequest());
    const auto* errorCard = findCard(errorOverview, "system.fuse");
    ASSERT_NE(errorCard, nullptr);
    EXPECT_EQ(errorCard->severity, "error");
    EXPECT_EQ(errorCard->errors.size(), 1u);
    EXPECT_TRUE(errorCard->warnings.empty());
    ASSERT_TRUE(metricValue(*errorCard, "alertable_error_rate"));
    EXPECT_DOUBLE_EQ(*metricValue(*errorCard, "alertable_error_rate"), 0.11);
}
