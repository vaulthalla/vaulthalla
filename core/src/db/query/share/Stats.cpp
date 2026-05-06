#include "db/query/share/Stats.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "stats/model/VaultShareStats.hpp"

#include <chrono>
#include <pqxx/pqxx>

namespace vh::db::query::share {

namespace {

using vh::db::encoding::parsePostgresTimestamp;
using vh::stats::model::VaultShareRecentEvent;
using vh::stats::model::VaultShareStats;
using vh::stats::model::VaultShareTopLink;

std::uint64_t unixTimestamp() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::optional<std::uint64_t> optionalTimestamp(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return static_cast<std::uint64_t>(parsePostgresTimestamp(field.as<std::string>()));
}

std::uint64_t timestamp(const pqxx::row& row, const char* column) {
    return optionalTimestamp(row, column).value_or(0);
}

std::uint64_t asUInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<long long>();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::optional<std::string> optionalString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    const auto value = field.as<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

void applySummary(VaultShareStats& stats, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();
    stats.activeLinks = asUInt64(row, "active_links");
    stats.expiredLinks = asUInt64(row, "expired_links");
    stats.revokedLinks = asUInt64(row, "revoked_links");
    stats.linksCreated24h = asUInt64(row, "links_created_24h");
    stats.linksRevoked24h = asUInt64(row, "links_revoked_24h");
    stats.publicLinks = asUInt64(row, "public_links");
    stats.emailValidatedLinks = asUInt64(row, "email_validated_links");
}

void applyEventWindow(VaultShareStats& stats, const pqxx::result& res) {
    if (res.empty()) return;
    const auto row = res.one_row();
    stats.downloads24h = asUInt64(row, "downloads_24h");
    stats.uploads24h = asUInt64(row, "uploads_24h");
    stats.deniedAttempts24h = asUInt64(row, "denied_attempts_24h");
    stats.rateLimitedAttempts24h = asUInt64(row, "rate_limited_attempts_24h");
    stats.failedAttempts24h = asUInt64(row, "failed_attempts_24h");
}

std::vector<VaultShareTopLink> topLinksFromResult(const pqxx::result& res) {
    std::vector<VaultShareTopLink> links;
    links.reserve(res.size());
    for (const auto& row : res) {
        links.push_back({
            .shareId = row["share_id"].as<std::string>(""),
            .label = row["label"].as<std::string>(""),
            .rootPath = row["root_path"].as<std::string>(""),
            .linkType = row["link_type"].as<std::string>("unknown"),
            .accessMode = row["access_mode"].as<std::string>("unknown"),
            .accessCount = asUInt64(row, "access_count"),
            .downloadCount = asUInt64(row, "download_count"),
            .uploadCount = asUInt64(row, "upload_count"),
            .lastAccessedAt = optionalTimestamp(row, "last_accessed_at"),
        });
    }
    return links;
}

std::vector<VaultShareRecentEvent> recentEventsFromResult(const pqxx::result& res) {
    std::vector<VaultShareRecentEvent> events;
    events.reserve(res.size());
    for (const auto& row : res) {
        events.push_back({
            .id = asUInt64(row, "id"),
            .shareId = optionalString(row, "share_id"),
            .eventType = row["event_type"].as<std::string>("unknown"),
            .status = row["status"].as<std::string>("unknown"),
            .targetPath = optionalString(row, "target_path"),
            .bytesTransferred = asUInt64(row, "bytes_transferred"),
            .errorCode = optionalString(row, "error_code"),
            .errorMessage = optionalString(row, "error_message"),
            .createdAt = timestamp(row, "created_at"),
        });
    }
    return events;
}

}

std::shared_ptr<::vh::stats::model::VaultShareStats> Stats::getVaultShareStats(const std::uint32_t vaultId) {
    return Transactions::exec("ShareStats::getVaultShareStats", [&](pqxx::work& txn) {
        auto stats = std::make_shared<VaultShareStats>();
        stats->vaultId = vaultId;
        stats->checkedAt = unixTimestamp();

        applySummary(*stats, txn.exec(pqxx::prepped{"share_stats.summary"}, vaultId));
        applyEventWindow(*stats, txn.exec(pqxx::prepped{"share_stats.event_window"}, vaultId));
        stats->topLinksByAccess = topLinksFromResult(txn.exec(pqxx::prepped{"share_stats.top_links"}, vaultId));
        stats->recentShareEvents = recentEventsFromResult(txn.exec(pqxx::prepped{"share_stats.recent_events"}, vaultId));

        return stats;
    });
}

}
