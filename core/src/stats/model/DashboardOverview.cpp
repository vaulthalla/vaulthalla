#include "stats/model/DashboardOverview.hpp"

#include "db/query/stats/DbStats.hpp"
#include "db/query/stats/OperationStats.hpp"
#include "db/query/stats/RetentionStats.hpp"
#include "db/query/stats/Snapshot.hpp"
#include "fs/cache/Registry.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/CacheStats.hpp"
#include "stats/model/ConnectionStats.hpp"
#include "stats/model/DbStats.hpp"
#include "stats/model/FuseStats.hpp"
#include "stats/model/OperationStats.hpp"
#include "stats/model/RetentionStats.hpp"
#include "stats/model/StatsTrends.hpp"
#include "stats/model/StorageBackendStats.hpp"
#include "stats/model/SystemHealth.hpp"
#include "stats/model/ThreadPoolStats.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

namespace vh::stats::model {

namespace {

struct DashboardOverviewSectionDescriptor {
    std::string id;
    std::string title;
    std::string description;
    std::string href;
};

struct DashboardOverviewCardDescriptor {
    std::string id;
    std::string sectionId;
    std::string title;
    std::string description;
    std::string href;
    std::string variant;
    std::string size;
};

std::uint64_t dashboardOverviewUnixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

template <typename T>
nlohmann::json dashboardOverviewNullable(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

int dashboardOverviewSeverityRank(const std::string& severity) {
    if (severity == "error") return 5;
    if (severity == "warning") return 4;
    if (severity == "unknown") return 3;
    if (severity == "info") return 2;
    if (severity == "unavailable") return 1;
    return 0;
}

std::string dashboardOverviewWorstSeverity(const std::string& a, const std::string& b) {
    return dashboardOverviewSeverityRank(b) > dashboardOverviewSeverityRank(a) ? b : a;
}

std::string dashboardOverviewSeverityFromStatus(const std::string& status) {
    if (status == "healthy" || status == "idle" || status == "normal" || status == "ready") return "healthy";
    if (status == "info" || status == "syncing" || status == "success") return "info";
    if (status == "warning" || status == "degraded" || status == "pressured" || status == "stale") return "warning";
    if (status == "error" || status == "critical" || status == "saturated" || status == "failing" || status == "stalled" || status == "diverged" || status == "overdue") return "error";
    if (status == "unavailable" || status == "disabled") return "unavailable";
    return "unknown";
}

std::string dashboardOverviewFormatCount(const std::uint64_t value) {
    return std::to_string(value);
}

std::string dashboardOverviewFormatRatio(const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2fx", value);
    return buffer;
}

std::string dashboardOverviewFormatPercent(const double ratio) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", ratio * 100.0);
    return buffer;
}

std::string dashboardOverviewFormatBytes(const std::uint64_t value) {
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    char buffer[48];
    if (value >= static_cast<std::uint64_t>(gib)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GiB", static_cast<double>(value) / gib);
    } else if (value >= static_cast<std::uint64_t>(mib)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(value) / mib);
    } else if (value >= static_cast<std::uint64_t>(kib)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(value) / kib);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(value));
    }
    return buffer;
}

std::string dashboardOverviewFormatDuration(const std::uint64_t seconds) {
    char buffer[48];
    if (seconds >= 86400) {
        std::snprintf(buffer, sizeof(buffer), "%llud", static_cast<unsigned long long>(seconds / 86400));
    } else if (seconds >= 3600) {
        std::snprintf(buffer, sizeof(buffer), "%lluh", static_cast<unsigned long long>(seconds / 3600));
    } else if (seconds >= 60) {
        std::snprintf(buffer, sizeof(buffer), "%llum", static_cast<unsigned long long>(seconds / 60));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llus", static_cast<unsigned long long>(seconds));
    }
    return buffer;
}

void dashboardOverviewAddMetric(
    DashboardCardSummary& card,
    std::string key,
    std::string label,
    std::string value,
    std::string tone = "unknown",
    std::optional<double> numericValue = std::nullopt,
    std::optional<std::string> unit = std::nullopt
) {
    DashboardMetricSummary metric;
    metric.key = std::move(key);
    metric.label = std::move(label);
    metric.value = std::move(value);
    metric.unit = std::move(unit);
    metric.tone = std::move(tone);
    metric.numericValue = numericValue;
    card.metrics.push_back(std::move(metric));
}

void dashboardOverviewAddIssue(
    DashboardCardSummary& card,
    std::string code,
    std::string severity,
    std::string message,
    std::optional<std::string> metricKey = std::nullopt
) {
    DashboardIssueSummary issue{
        .code = std::move(code),
        .severity = std::move(severity),
        .message = std::move(message),
        .href = card.href,
        .metricKey = std::move(metricKey),
    };

    if (issue.severity == "error") card.errors.push_back(std::move(issue));
    else card.warnings.push_back(std::move(issue));
}

DashboardCardSummary dashboardOverviewBaseCard(const DashboardOverviewCardDescriptor& descriptor) {
    DashboardCardSummary card;
    card.id = descriptor.id;
    card.sectionId = descriptor.sectionId;
    card.title = descriptor.title;
    card.description = descriptor.description;
    card.href = descriptor.href;
    card.variant = descriptor.variant;
    card.size = descriptor.size;
    card.checkedAt = dashboardOverviewUnixTimestamp();
    return card;
}

DashboardCardSummary dashboardOverviewUnavailableCard(
    const DashboardOverviewCardDescriptor& descriptor,
    const std::string& reason
) {
    auto card = dashboardOverviewBaseCard(descriptor);
    card.available = false;
    card.severity = "unavailable";
    card.unavailableReason = reason;
    card.summary = reason;
    return card;
}

std::vector<DashboardOverviewSectionDescriptor> dashboardOverviewSectionDescriptors() {
    return {
        {"runtime", "Runtime", "Runtime services, worker pressure, and connected sessions.", "/dashboard/runtime"},
        {"filesystem", "Filesystem", "FUSE activity and preview cache readiness.", "/dashboard/filesystem"},
        {"storage", "Storage", "Backing providers, database health, and cleanup pressure.", "/dashboard/storage"},
        {"operations", "Operations", "Queued work, active transfers, and stuck-operation pressure.", "/dashboard/operations"},
        {"trends", "Trends", "Historical samples for live telemetry surfaces.", "/dashboard/trends"},
    };
}

std::vector<DashboardOverviewCardDescriptor> dashboardOverviewCardDescriptors() {
    return {
        {"system.health", "runtime", "System Health", "Core runtime, protocol, dependency, FUSE, and shell readiness.", "/dashboard/runtime#system-health", "hero", "3x2"},
        {"system.threadpools", "runtime", "Thread Pools", "Runtime worker pressure across FUSE, sync, thumbnails, HTTP, and stats.", "/dashboard/runtime#thread-pools", "visual", "2x1"},
        {"system.connections", "runtime", "Connection Health", "Websocket session mix and unauthenticated buildup.", "/dashboard/runtime#connections", "visual", "2x1"},
        {"system.fuse", "filesystem", "FUSE Filesystem", "Live filesystem operation volume, errors, latency, and open handles.", "/dashboard/filesystem#fuse", "visual", "2x1"},
        {"system.fs_cache", "filesystem", "FS Cache", "Filesystem cache hit rate, usage, and churn.", "/dashboard/filesystem#fs-cache", "visual", "2x1"},
        {"system.http_cache", "filesystem", "HTTP Preview Cache", "Preview cache hit rate, usage, and churn.", "/dashboard/filesystem#http-cache", "visual", "2x1"},
        {"system.storage", "storage", "Storage Backend", "Local and S3 vault backend configuration and free-space posture.", "/dashboard/storage#storage-backend", "visual", "2x1"},
        {"system.db", "storage", "Database Health", "Database connectivity, connection pressure, cache hit ratio, and table size.", "/dashboard/storage#database", "visual", "2x1"},
        {"system.retention", "storage", "Retention / Cleanup", "Trash, audit, sync, share, and cache cleanup backlog.", "/dashboard/storage#retention", "visual", "2x1"},
        {"system.operations", "operations", "Operation Queue", "Pending, active, failed, and stalled filesystem/share work.", "/dashboard/operations#operation-queue", "visual", "2x1"},
        {"system.trends", "trends", "Trends", "Recently collected stats snapshot series.", "/dashboard/trends#trends", "summary", "2x1"},
    };
}

DashboardCardSummary dashboardOverviewBuildSystemHealth(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto health = SystemHealth::snapshot();
    const auto status = health.overallStatusString();

    card.checkedAt = health.summary.checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(status);
    if (status == "healthy" && health.shell.adminUidBound && !*health.shell.adminUidBound) {
        card.severity = "info";
        card.summary = "Core runtime is healthy. CLI shell admin binding is not configured.";
    } else {
        card.summary = card.severity == "healthy" ? "Runtime services and dependencies are healthy." : "Runtime health needs attention.";
    }

    dashboardOverviewAddMetric(card, "services", "Services", std::to_string(health.summary.servicesReady) + "/" + std::to_string(health.summary.servicesTotal), card.severity);
    dashboardOverviewAddMetric(card, "protocols", "Protocols", std::to_string(health.summary.protocolsReady) + "/" + std::to_string(health.summary.protocolsTotal), health.summary.protocolsReady == health.summary.protocolsTotal ? "healthy" : "warning");
    dashboardOverviewAddMetric(card, "deps", "Deps", std::to_string(health.summary.depsReady) + "/" + std::to_string(health.summary.depsTotal), health.summary.depsReady == health.summary.depsTotal ? "healthy" : "warning");
    dashboardOverviewAddMetric(
        card,
        "shell_admin_uid",
        "CLI Shell",
        !health.shell.adminUidBound ? "unknown" : *health.shell.adminUidBound ? "bound" : "setup",
        !health.shell.adminUidBound ? "unknown" : *health.shell.adminUidBound ? "healthy" : "info"
    );

    if (status != "healthy") {
        const auto severity = card.severity == "error" ? "error" : "warning";
        dashboardOverviewAddIssue(card, "system.health.unhealthy", severity, "System health is " + status + ".");
    }
    if (!health.deps.fuseSession) dashboardOverviewAddIssue(card, "system.health.fuse_missing", "warning", "FUSE session is not present.", "fuse");

    return card;
}

DashboardCardSummary dashboardOverviewBuildThreadPools(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = ThreadPoolManagerSnapshot::snapshot();
    card.checkedAt = stats.checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats.overallStatus);
    card.summary = stats.overallStatus == "healthy" ? "Runtime workers are available." : "Runtime worker pressure needs attention.";

    dashboardOverviewAddMetric(card, "workers", "Workers", dashboardOverviewFormatCount(stats.totalWorkerCount), card.severity);
    dashboardOverviewAddMetric(card, "queue", "Queue", dashboardOverviewFormatCount(stats.totalQueueDepth), stats.totalQueueDepth == 0 ? "healthy" : "warning");
    dashboardOverviewAddMetric(card, "pressure", "Max Pressure", dashboardOverviewFormatRatio(stats.maxPressureRatio), card.severity, stats.maxPressureRatio);
    dashboardOverviewAddMetric(card, "pressured", "Pressured", dashboardOverviewFormatCount(stats.pressuredPoolCount), stats.pressuredPoolCount == 0 ? "healthy" : "warning");
    dashboardOverviewAddMetric(card, "saturated", "Saturated", dashboardOverviewFormatCount(stats.saturatedPoolCount), stats.saturatedPoolCount == 0 ? "healthy" : "error");

    if (stats.overallStatus == "pressured") {
        dashboardOverviewAddIssue(card, "system.threadpools.pressured", "warning", std::to_string(stats.pressuredPoolCount) + " thread pool(s) are pressured.", "pressure");
    } else if (stats.overallStatus == "saturated") {
        dashboardOverviewAddIssue(card, "system.threadpools.saturated", "error", std::to_string(stats.saturatedPoolCount) + " thread pool(s) are saturated.", "pressure");
    } else if (stats.overallStatus == "degraded") {
        dashboardOverviewAddIssue(card, "system.threadpools.degraded", "error", "One or more thread pools are degraded.", "workers");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildConnections(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = ConnectionStats::snapshot();
    card.checkedAt = stats.checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats.status);
    card.summary = stats.status == "healthy" ? "Connected sessions look normal." : "Connection mix needs attention.";

    dashboardOverviewAddMetric(card, "sessions", "Sessions", dashboardOverviewFormatCount(stats.activeWsSessionsTotal), card.severity);
    dashboardOverviewAddMetric(card, "human", "Human", dashboardOverviewFormatCount(stats.activeHumanSessions), "info");
    dashboardOverviewAddMetric(card, "share", "Share", dashboardOverviewFormatCount(stats.activeShareSessions + stats.activeSharePendingSessions), "info");
    dashboardOverviewAddMetric(card, "unauthenticated", "Unauth", dashboardOverviewFormatCount(stats.activeUnauthenticatedSessions), stats.activeUnauthenticatedSessions == 0 ? "healthy" : "warning");

    if (stats.status == "warning") {
        dashboardOverviewAddIssue(card, "system.connections.unauthenticated", "warning", "Unauthenticated websocket sessions are active.", "unauthenticated");
    } else if (stats.status == "critical") {
        dashboardOverviewAddIssue(card, "system.connections.unauthenticated_critical", "error", "Unauthenticated websocket sessions are accumulating.", "unauthenticated");
    } else if (stats.status == "unknown") {
        dashboardOverviewAddIssue(card, "system.connections.unknown", "warning", "Session manager state is unavailable.");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildFuse(const DashboardOverviewCardDescriptor& descriptor) {
    const auto statsPtr = runtime::Deps::get().fuseStats;
    if (!statsPtr) return dashboardOverviewUnavailableCard(descriptor, "FUSE stats are not initialized.");

    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = statsPtr->snapshot();
    card.checkedAt = stats.checkedAt;

    if (stats.totalOps == 0) card.severity = "info";
    else if (stats.errorRate > 0.10) card.severity = "error";
    else if (stats.errorRate > 0.02) card.severity = "warning";
    else if (stats.errorRate > 0.0) card.severity = "info";
    else card.severity = "healthy";

    card.summary = stats.totalOps == 0 ? "No FUSE traffic has been observed yet." : "FUSE operation telemetry is live.";
    dashboardOverviewAddMetric(card, "ops", "Ops", dashboardOverviewFormatCount(stats.totalOps), card.severity);
    dashboardOverviewAddMetric(card, "error_rate", "Errors", dashboardOverviewFormatPercent(stats.errorRate), card.severity, stats.errorRate);
    dashboardOverviewAddMetric(card, "open_handles", "Open Handles", dashboardOverviewFormatCount(stats.openHandlesCurrent), "info");

    if (stats.errorRate > 0.10) {
        dashboardOverviewAddIssue(card, "system.fuse.error_rate_high", "error", "FUSE error rate is above 10%.", "error_rate");
    } else if (stats.errorRate > 0.02) {
        dashboardOverviewAddIssue(card, "system.fuse.error_rate_elevated", "warning", "FUSE error rate is above 2%.", "error_rate");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildCache(const DashboardOverviewCardDescriptor& descriptor, const CacheStatsSnapshot& stats, const std::uint64_t checkedAt) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto requests = stats.hits + stats.misses;
    const auto hitRate = CacheStats::hit_rate(stats);
    card.checkedAt = checkedAt;
    card.severity = requests == 0 ? "info" : "healthy";
    card.summary = requests == 0 ? "No cache traffic has been observed yet." : "Cache telemetry is live.";

    dashboardOverviewAddMetric(card, "hit_rate", "Hit Rate", dashboardOverviewFormatPercent(hitRate), card.severity, hitRate);
    dashboardOverviewAddMetric(card, "used", "Used", dashboardOverviewFormatBytes(stats.used_bytes), "info", static_cast<double>(stats.used_bytes), "bytes");
    dashboardOverviewAddMetric(card, "evictions", "Evictions", dashboardOverviewFormatCount(stats.evictions), stats.evictions == 0 ? "healthy" : "info");

    return card;
}

DashboardCardSummary dashboardOverviewBuildFsCache(const DashboardOverviewCardDescriptor& descriptor) {
    const auto cache = runtime::Deps::get().fsCache;
    if (!cache) return dashboardOverviewUnavailableCard(descriptor, "Filesystem cache registry is not initialized.");

    const auto stats = cache->stats();
    if (!stats) return dashboardOverviewUnavailableCard(descriptor, "Filesystem cache stats are unavailable.");

    return dashboardOverviewBuildCache(descriptor, *stats, dashboardOverviewUnixTimestamp());
}

DashboardCardSummary dashboardOverviewBuildHttpCache(const DashboardOverviewCardDescriptor& descriptor) {
    const auto cache = runtime::Deps::get().httpCacheStats;
    if (!cache) return dashboardOverviewUnavailableCard(descriptor, "HTTP preview cache stats are not initialized.");
    return dashboardOverviewBuildCache(descriptor, cache->snapshot(), dashboardOverviewUnixTimestamp());
}

DashboardCardSummary dashboardOverviewBuildStorage(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = StorageBackendStats::snapshot();
    card.checkedAt = stats.checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats.overallStatus);
    card.summary = stats.overallStatus == "healthy" ? "Storage backends are healthy." : "Storage backend posture needs attention.";

    dashboardOverviewAddMetric(card, "vaults", "Vaults", dashboardOverviewFormatCount(stats.vaultCountTotal), card.severity);
    dashboardOverviewAddMetric(card, "active", "Active", dashboardOverviewFormatCount(stats.activeVaultCount), "healthy");
    dashboardOverviewAddMetric(card, "local", "Local", dashboardOverviewFormatCount(stats.localVaultCount), "info");
    dashboardOverviewAddMetric(card, "s3", "S3", dashboardOverviewFormatCount(stats.s3VaultCount), "info");

    if (stats.errorVaultCount > 0) {
        dashboardOverviewAddIssue(card, "system.storage.error_vaults", "error", std::to_string(stats.errorVaultCount) + " vault backend(s) are in error.", "vaults");
    }
    if (stats.degradedVaultCount > 0) {
        dashboardOverviewAddIssue(card, "system.storage.degraded_vaults", "warning", std::to_string(stats.degradedVaultCount) + " vault backend(s) are degraded.", "vaults");
    }
    if (stats.vaultCountTotal == 0) {
        dashboardOverviewAddIssue(card, "system.storage.no_vaults", "warning", "No vault backends are available.", "vaults");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildDb(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = vh::db::query::stats::DbStats::snapshot();
    if (!stats) return dashboardOverviewUnavailableCard(descriptor, "Database stats are unavailable.");

    card.checkedAt = stats->checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats->status);
    card.summary = stats->connected ? "Database telemetry is live." : "Database is not connected.";

    dashboardOverviewAddMetric(card, "size", "DB Size", dashboardOverviewFormatBytes(stats->dbSizeBytes), "info", static_cast<double>(stats->dbSizeBytes), "bytes");
    dashboardOverviewAddMetric(card, "connections", "Connections", dashboardOverviewFormatCount(stats->connectionsTotal), card.severity);
    dashboardOverviewAddMetric(card, "cache_hit", "Cache Hit", stats->cacheHitRatio ? dashboardOverviewFormatPercent(*stats->cacheHitRatio) : "unknown", stats->cacheHitRatio ? "healthy" : "unknown", stats->cacheHitRatio);

    if (!stats->connected) {
        dashboardOverviewAddIssue(card, "system.db.disconnected", "error", stats->error.value_or("Database connection is unavailable."));
    } else if (stats->status == "critical") {
        dashboardOverviewAddIssue(card, "system.db.critical", "error", "Database health is critical.");
    } else if (stats->status == "warning") {
        dashboardOverviewAddIssue(card, "system.db.warning", "warning", "Database health needs attention.");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildRetention(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = vh::db::query::stats::RetentionStats::snapshot();
    if (!stats) return dashboardOverviewUnavailableCard(descriptor, "Retention stats are unavailable.");

    card.checkedAt = stats->checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats->cleanupStatus);
    card.summary = stats->cleanupStatus == "healthy" ? "Cleanup pressure is under control." : "Cleanup backlog needs attention.";

    dashboardOverviewAddMetric(card, "trash", "Trash", dashboardOverviewFormatCount(stats->trashedFilesCount), "info");
    dashboardOverviewAddMetric(card, "overdue", "Overdue", dashboardOverviewFormatCount(stats->trashedFilesPastRetentionCount), stats->trashedFilesPastRetentionCount == 0 ? "healthy" : "warning");
    dashboardOverviewAddMetric(card, "cache_expired", "Expired Cache", dashboardOverviewFormatCount(stats->cacheEntriesExpired), stats->cacheEntriesExpired == 0 ? "healthy" : "warning");

    if (stats->cleanupStatus == "overdue") {
        dashboardOverviewAddIssue(card, "system.retention.overdue", "error", "Retention cleanup is overdue.");
    } else if (stats->cleanupStatus == "warning") {
        dashboardOverviewAddIssue(card, "system.retention.warning", "warning", "Some retained records are past configured cleanup thresholds.");
    } else if (stats->cleanupStatus == "unknown") {
        dashboardOverviewAddIssue(card, "system.retention.unknown", "warning", "Retention configuration or source tables are unavailable.");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildOperations(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = vh::db::query::stats::OperationStats::snapshot();
    if (!stats) return dashboardOverviewUnavailableCard(descriptor, "Operation queue stats are unavailable.");

    card.checkedAt = stats->checkedAt;
    card.severity = dashboardOverviewSeverityFromStatus(stats->overallStatus);
    card.summary = stats->overallStatus == "healthy" ? "No stuck work is visible." : "Operation queue needs attention.";

    dashboardOverviewAddMetric(card, "pending", "Pending", dashboardOverviewFormatCount(stats->pendingOperations), stats->pendingOperations == 0 ? "healthy" : "warning");
    dashboardOverviewAddMetric(card, "in_progress", "In Progress", dashboardOverviewFormatCount(stats->inProgressOperations), "info");
    dashboardOverviewAddMetric(card, "stalled", "Stalled", dashboardOverviewFormatCount(stats->stalledOperations + stats->stalledShareUploads), stats->stalledOperations + stats->stalledShareUploads == 0 ? "healthy" : "error");
    dashboardOverviewAddMetric(card, "failed_24h", "Failed 24h", dashboardOverviewFormatCount(stats->failedOperations24h + stats->failedShareUploads24h), stats->failedOperations24h + stats->failedShareUploads24h == 0 ? "healthy" : "warning");

    if (stats->stalledOperations + stats->stalledShareUploads > 0) {
        dashboardOverviewAddIssue(card, "system.operations.stalled", "error", "One or more operations or uploads appear stalled.", "stalled");
    } else if (stats->failedOperations24h + stats->failedShareUploads24h > 0) {
        dashboardOverviewAddIssue(card, "system.operations.failed_recent", "warning", "Operations or uploads failed in the last 24 hours.");
    } else if (stats->pendingOperations + stats->inProgressOperations + stats->activeShareUploads > 0) {
        dashboardOverviewAddIssue(card, "system.operations.active_work", "warning", "Queued or active work is present.");
    }

    return card;
}

DashboardCardSummary dashboardOverviewBuildTrends(const DashboardOverviewCardDescriptor& descriptor) {
    auto card = dashboardOverviewBaseCard(descriptor);
    const auto stats = vh::db::query::stats::Snapshot::systemTrends(168);
    if (!stats) return dashboardOverviewUnavailableCard(descriptor, "Trend snapshots are unavailable.");

    card.checkedAt = stats->checkedAt;
    const auto seriesCount = stats->series.size();
    std::uint64_t pointCount = 0;
    std::uint64_t latestSampleAt = 0;
    for (const auto& series : stats->series) {
        pointCount += series.points.size();
        for (const auto& point : series.points) latestSampleAt = std::max(latestSampleAt, point.createdAt);
    }

    card.severity = pointCount == 0 ? "info" : "healthy";
    card.summary = pointCount == 0 ? "No historical trend samples are available yet." : "Historical trend samples are available.";
    dashboardOverviewAddMetric(card, "window", "Window", std::to_string(stats->windowHours) + "h", "info");
    if (latestSampleAt > 0) {
        const auto now = dashboardOverviewUnixTimestamp();
        const auto latestAge = now > latestSampleAt ? now - latestSampleAt : 0;
        dashboardOverviewAddMetric(card, "latest_sample_age", "Latest", dashboardOverviewFormatDuration(latestAge), latestAge <= 3600 ? "healthy" : "info", static_cast<double>(latestAge), "seconds");
        dashboardOverviewAddMetric(card, "coverage", "Coverage", dashboardOverviewFormatCount(seriesCount) + " series", card.severity, static_cast<double>(seriesCount));
    } else {
        dashboardOverviewAddMetric(card, "coverage", "Coverage", "no data", "info", 0.0);
    }
    dashboardOverviewAddMetric(card, "series", "Series", dashboardOverviewFormatCount(seriesCount), card.severity);
    dashboardOverviewAddMetric(card, "points", "Points", dashboardOverviewFormatCount(pointCount), card.severity);

    return card;
}

DashboardCardSummary dashboardOverviewBuildCard(const DashboardOverviewCardDescriptor& descriptor) {
    try {
        if (descriptor.id == "system.health") return dashboardOverviewBuildSystemHealth(descriptor);
        if (descriptor.id == "system.threadpools") return dashboardOverviewBuildThreadPools(descriptor);
        if (descriptor.id == "system.connections") return dashboardOverviewBuildConnections(descriptor);
        if (descriptor.id == "system.fuse") return dashboardOverviewBuildFuse(descriptor);
        if (descriptor.id == "system.fs_cache") return dashboardOverviewBuildFsCache(descriptor);
        if (descriptor.id == "system.http_cache") return dashboardOverviewBuildHttpCache(descriptor);
        if (descriptor.id == "system.storage") return dashboardOverviewBuildStorage(descriptor);
        if (descriptor.id == "system.db") return dashboardOverviewBuildDb(descriptor);
        if (descriptor.id == "system.retention") return dashboardOverviewBuildRetention(descriptor);
        if (descriptor.id == "system.operations") return dashboardOverviewBuildOperations(descriptor);
        if (descriptor.id == "system.trends") return dashboardOverviewBuildTrends(descriptor);
        return dashboardOverviewUnavailableCard(descriptor, "Unknown dashboard card.");
    } catch (const std::exception& e) {
        return dashboardOverviewUnavailableCard(descriptor, e.what());
    }
}

DashboardOverviewCardDescriptor dashboardOverviewDescriptorForUnknown(const std::string& id) {
    return {
        .id = id,
        .sectionId = "runtime",
        .title = id.empty() ? "Unknown Card" : id,
        .description = "Requested dashboard card is not registered.",
        .href = "/dashboard",
        .variant = "summary",
        .size = "2x1",
    };
}

std::vector<DashboardOverviewCardDescriptor> dashboardOverviewRequestedDescriptors(const DashboardOverviewRequest& request) {
    const auto descriptors = dashboardOverviewCardDescriptors();
    if (request.cards.empty()) return descriptors;

    std::unordered_map<std::string, DashboardOverviewCardDescriptor> byId;
    for (const auto& descriptor : descriptors) byId.emplace(descriptor.id, descriptor);

    std::vector<DashboardOverviewCardDescriptor> out;
    out.reserve(request.cards.size());
    for (const auto& requested : request.cards) {
        auto descriptor = byId.contains(requested.id) ? byId.at(requested.id) : dashboardOverviewDescriptorForUnknown(requested.id);
        if (!requested.variant.empty()) descriptor.variant = requested.variant;
        if (!requested.size.empty()) descriptor.size = requested.size;
        out.push_back(std::move(descriptor));
    }
    return out;
}

DashboardSectionSummary dashboardOverviewBuildSection(
    const DashboardOverviewSectionDescriptor& descriptor,
    const std::vector<DashboardCardSummary>& cards
) {
    DashboardSectionSummary section;
    section.id = descriptor.id;
    section.title = descriptor.title;
    section.description = descriptor.description;
    section.href = descriptor.href;
    section.severity = "healthy";
    section.checkedAt = dashboardOverviewUnixTimestamp();

    bool hasCard = false;
    bool hasUnknown = false;
    for (const auto& card : cards) {
        if (card.sectionId != descriptor.id) continue;
        hasCard = true;
        if (card.severity == "unknown") hasUnknown = true;
        if (card.available) section.severity = dashboardOverviewWorstSeverity(section.severity, card.severity);
        section.warningCount += static_cast<std::uint32_t>(card.warnings.size());
        section.errorCount += static_cast<std::uint32_t>(card.errors.size());
        section.checkedAt = std::max(section.checkedAt, card.checkedAt);

        for (const auto& metric : card.metrics) {
            if (section.metrics.size() >= 4) break;
            auto sectionMetric = metric;
            sectionMetric.href = card.href;
            section.metrics.push_back(std::move(sectionMetric));
        }
        for (const auto& warning : card.warnings) section.warnings.push_back(warning);
        for (const auto& error : card.errors) section.errors.push_back(error);
    }

    if (!hasCard) {
        section.severity = "unavailable";
        section.summary = "No dashboard cards are registered for this section.";
    } else if (section.errorCount > 0) {
        section.summary = section.title + " has errors that need attention.";
    } else if (section.warningCount > 0) {
        section.summary = section.title + " has warnings to review.";
    } else if (hasUnknown) {
        section.severity = dashboardOverviewWorstSeverity(section.severity, "unknown");
        section.summary = section.title + " has unknown telemetry.";
    } else {
        section.summary = section.title + " looks healthy.";
    }

    return section;
}

void dashboardOverviewAddAttention(DashboardOverview& overview, const DashboardCardSummary& card, const DashboardIssueSummary& issue) {
    overview.attention.push_back({
        .code = issue.code,
        .severity = issue.severity,
        .cardId = card.id,
        .title = card.title,
        .message = issue.message,
        .href = issue.href ? issue.href : std::optional<std::string>(card.href),
        .metricKey = issue.metricKey,
    });
}

}

DashboardOverview DashboardOverview::snapshot(const DashboardOverviewRequest& request) {
    DashboardOverview overview;
    overview.checkedAt = dashboardOverviewUnixTimestamp();

    const auto descriptors = dashboardOverviewRequestedDescriptors(request);
    overview.cards.reserve(descriptors.size());
    for (const auto& descriptor : descriptors) {
        auto card = dashboardOverviewBuildCard(descriptor);
        card.variant = descriptor.variant;
        card.size = descriptor.size;
        overview.checkedAt = std::max(overview.checkedAt, card.checkedAt);
        overview.cards.push_back(std::move(card));
    }

    for (const auto& descriptor : dashboardOverviewSectionDescriptors()) {
        overview.sections.push_back(dashboardOverviewBuildSection(descriptor, overview.cards));
    }

    std::string worst = "healthy";
    bool sawVisibleStatus = false;
    bool sawUnknown = false;
    for (const auto& card : overview.cards) {
        if (!card.available) continue;
        sawVisibleStatus = true;
        if (card.severity == "unknown") sawUnknown = true;
        worst = dashboardOverviewWorstSeverity(worst, card.severity);
        overview.warningCount += static_cast<std::uint32_t>(card.warnings.size());
        overview.errorCount += static_cast<std::uint32_t>(card.errors.size());
        for (const auto& error : card.errors) dashboardOverviewAddAttention(overview, card, error);
        for (const auto& warning : card.warnings) dashboardOverviewAddAttention(overview, card, warning);
    }

    if (overview.errorCount > 0) overview.overallStatus = "error";
    else if (overview.warningCount > 0) overview.overallStatus = "warning";
    else if (!sawVisibleStatus) overview.overallStatus = "unavailable";
    else if (sawUnknown || worst == "unknown") overview.overallStatus = "unknown";
    else overview.overallStatus = worst;

    std::sort(overview.attention.begin(), overview.attention.end(), [](const auto& a, const auto& b) {
        if (dashboardOverviewSeverityRank(a.severity) != dashboardOverviewSeverityRank(b.severity))
            return dashboardOverviewSeverityRank(a.severity) > dashboardOverviewSeverityRank(b.severity);
        return a.title < b.title;
    });

    return overview;
}

DashboardOverviewRequest dashboardOverviewRequestFromJson(const nlohmann::json& payload) {
    DashboardOverviewRequest request;
    if (!payload.is_object()) return request;

    request.scope = payload.value("scope", request.scope);
    request.mode = payload.value("mode", request.mode);

    const auto cardsIt = payload.find("cards");
    if (cardsIt != payload.end() && cardsIt->is_array()) {
        for (const auto& raw : *cardsIt) {
            if (!raw.is_object()) continue;
            DashboardCardRequest card{
                .id = raw.value("id", std::string{}),
                .variant = raw.value("variant", std::string{}),
                .size = raw.value("size", std::string{}),
            };
            if (!card.id.empty()) request.cards.push_back(std::move(card));
        }
    }

    return request;
}

void to_json(nlohmann::json& j, const DashboardMetricSummary& metric) {
    j = nlohmann::json{
        {"key", metric.key},
        {"label", metric.label},
        {"value", metric.value},
        {"unit", dashboardOverviewNullable(metric.unit)},
        {"tone", metric.tone},
        {"numeric_value", dashboardOverviewNullable(metric.numericValue)},
        {"href", dashboardOverviewNullable(metric.href)},
    };
}

void to_json(nlohmann::json& j, const DashboardIssueSummary& issue) {
    j = nlohmann::json{
        {"code", issue.code},
        {"severity", issue.severity},
        {"message", issue.message},
        {"href", dashboardOverviewNullable(issue.href)},
        {"metric_key", dashboardOverviewNullable(issue.metricKey)},
    };
}

void to_json(nlohmann::json& j, const DashboardAttentionItem& item) {
    j = nlohmann::json{
        {"code", item.code},
        {"severity", item.severity},
        {"card_id", item.cardId},
        {"title", item.title},
        {"message", item.message},
        {"href", dashboardOverviewNullable(item.href)},
        {"metric_key", dashboardOverviewNullable(item.metricKey)},
    };
}

void to_json(nlohmann::json& j, const DashboardCardSummary& card) {
    j = nlohmann::json{
        {"id", card.id},
        {"section_id", card.sectionId},
        {"title", card.title},
        {"description", card.description},
        {"href", card.href},
        {"variant", card.variant},
        {"size", card.size},
        {"severity", card.severity},
        {"available", card.available},
        {"unavailable_reason", dashboardOverviewNullable(card.unavailableReason)},
        {"summary", card.summary},
        {"metrics", card.metrics},
        {"warnings", card.warnings},
        {"errors", card.errors},
        {"checked_at", card.checkedAt},
    };
}

void to_json(nlohmann::json& j, const DashboardSectionSummary& section) {
    j = nlohmann::json{
        {"id", section.id},
        {"title", section.title},
        {"description", section.description},
        {"href", section.href},
        {"severity", section.severity},
        {"warning_count", section.warningCount},
        {"error_count", section.errorCount},
        {"summary", section.summary},
        {"metrics", section.metrics},
        {"warnings", section.warnings},
        {"errors", section.errors},
        {"checked_at", section.checkedAt},
    };
}

void to_json(nlohmann::json& j, const DashboardOverview& overview) {
    j = nlohmann::json{
        {"overall_status", overview.overallStatus},
        {"warning_count", overview.warningCount},
        {"error_count", overview.errorCount},
        {"checked_at", overview.checkedAt},
        {"attention", overview.attention},
        {"sections", overview.sections},
        {"cards", overview.cards},
    };
}

}
