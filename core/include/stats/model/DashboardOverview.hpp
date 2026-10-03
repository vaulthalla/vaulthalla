#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct DashboardMetricSummary {
    std::string key;
    std::string label;
    std::string value;
    std::optional<std::string> unit;
    std::string tone = "unknown";
    std::optional<double> numericValue;
    std::optional<std::string> href;
};

struct DashboardGraphPoint {
    std::uint64_t createdAt = 0;
    double value = 0.0;
};

struct DashboardGraphSeries {
    std::string key;
    std::string label;
    std::string unit;
    std::string tone = "info";
    std::vector<DashboardGraphPoint> points;
};

struct DashboardIssueSummary {
    std::string code;
    std::string severity;
    std::string message;
    std::optional<std::string> href;
    std::optional<std::string> metricKey;
};

struct DashboardAttentionItem {
    std::string code;
    std::string severity;
    std::string cardId;
    std::string title;
    std::string message;
    std::optional<std::string> href;
    std::optional<std::string> metricKey;
};

struct DashboardCardRequest {
    std::string id;
    std::string variant;
    std::string size;
};

struct DashboardOverviewRequest {
    std::string scope = "system";
    std::string mode = "dashboard_home";
    std::vector<DashboardCardRequest> cards;
};

struct DashboardCardSummary {
    std::string id;
    std::string sectionId;
    std::string title;
    std::string description;
    std::string href;
    std::string variant = "tiles";
    std::string size = "2x1";
    std::string severity = "unknown";
    bool available = true;
    std::optional<std::string> unavailableReason;
    std::string summary;
    std::vector<DashboardMetricSummary> metrics;
    std::vector<DashboardGraphSeries> series;
    std::vector<DashboardIssueSummary> warnings;
    std::vector<DashboardIssueSummary> errors;
    std::uint64_t checkedAt = 0;
};

struct DashboardSectionSummary {
    std::string id;
    std::string title;
    std::string description;
    std::string href;
    std::string severity = "unknown";
    std::uint32_t warningCount = 0;
    std::uint32_t errorCount = 0;
    std::string summary;
    std::vector<DashboardMetricSummary> metrics;
    std::vector<DashboardIssueSummary> warnings;
    std::vector<DashboardIssueSummary> errors;
    std::uint64_t checkedAt = 0;
};

struct DashboardOverview {
    std::string overallStatus = "unknown";
    std::uint32_t warningCount = 0;
    std::uint32_t errorCount = 0;
    std::uint64_t checkedAt = 0;
    std::vector<DashboardSectionSummary> sections;
    std::vector<DashboardCardSummary> cards;
    std::vector<DashboardAttentionItem> attention;

    static DashboardOverview snapshot(const DashboardOverviewRequest& request = {});
    // The default dashboard's overall_status / warning_count / error_count / checked_at from the same cards and the
    // same aggregation as snapshot(), without trend series or sections. Cards and attention are filled but callers
    // serialize only the four summary fields (stats.dashboard.severity).
    static DashboardOverview severity();

private:
    static DashboardOverview build(const DashboardOverviewRequest& request, bool full);
};

// The stats.dashboard.severity payload: {overall_status, error_count, warning_count, checked_at}.
nlohmann::json dashboardSeverityJson(const DashboardOverview& overview);

DashboardOverviewRequest dashboardOverviewRequestFromJson(const nlohmann::json& payload);

void to_json(nlohmann::json& j, const DashboardMetricSummary& metric);
void to_json(nlohmann::json& j, const DashboardGraphPoint& point);
void to_json(nlohmann::json& j, const DashboardGraphSeries& series);
void to_json(nlohmann::json& j, const DashboardIssueSummary& issue);
void to_json(nlohmann::json& j, const DashboardAttentionItem& item);
void to_json(nlohmann::json& j, const DashboardCardSummary& card);
void to_json(nlohmann::json& j, const DashboardSectionSummary& section);
void to_json(nlohmann::json& j, const DashboardOverview& overview);

}
