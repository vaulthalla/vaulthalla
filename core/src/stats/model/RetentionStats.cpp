#include "stats/model/RetentionStats.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableRetentionValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

std::uint64_t twiceDaysAsSeconds(const std::uint64_t days) {
    return days * 2 * 24 * 60 * 60;
}

}

void RetentionStats::finalize() {
    if (trashRetentionDays == 0 || syncEventRetentionDays == 0 || syncEventMaxEntries == 0 ||
        auditLogRetentionDays == 0 || cacheExpiryDays == 0) {
        cleanupStatus = "unknown";
        return;
    }

    const auto largeTrashBacklog = trashedFilesPastRetentionCount > 100 || trashedBytesPastRetention > 1024ull * 1024ull * 1024ull;
    const auto largeSyncBacklog = syncEventsPastRetentionCount > std::max<std::uint64_t>(1000, syncEventMaxEntries / 10);
    const auto largeAuditBacklog = auditLogEntriesPastRetentionCount > 1000;
    const auto largeCacheBacklog = cacheEntriesExpired > 1000 || (cacheMaxSizeBytes > 0 && cacheBytesTotal > cacheMaxSizeBytes);
    const auto veryOldTrash = oldestTrashedAgeSeconds && *oldestTrashedAgeSeconds > twiceDaysAsSeconds(trashRetentionDays);
    const auto veryOldAudit = oldestAuditLogAgeSeconds && *oldestAuditLogAgeSeconds > twiceDaysAsSeconds(auditLogRetentionDays);

    if (largeTrashBacklog || largeSyncBacklog || largeAuditBacklog || largeCacheBacklog || veryOldTrash || veryOldAudit) {
        cleanupStatus = "overdue";
        return;
    }

    if (trashedFilesPastRetentionCount > 0 || syncEventsPastRetentionCount > 0 ||
        auditLogEntriesPastRetentionCount > 0 || cacheEntriesExpired > 0 || cacheEvictionCandidates > 0) {
        cleanupStatus = "warning";
        return;
    }

    cleanupStatus = "healthy";
}

void to_json(nlohmann::json& j, const RetentionStats& stats) {
    j = nlohmann::json{
        {"vault_id", nullableRetentionValue(stats.vaultId)},
        {"scope", stats.scope},
        {"cleanup_status", stats.cleanupStatus},
        {"trash_retention_days", stats.trashRetentionDays},
        {"sync_event_retention_days", stats.syncEventRetentionDays},
        {"sync_event_max_entries", stats.syncEventMaxEntries},
        {"audit_log_retention_days", stats.auditLogRetentionDays},
        {"cache_expiry_days", stats.cacheExpiryDays},
        {"cache_max_size_bytes", stats.cacheMaxSizeBytes},
        {"trashed_files_count", stats.trashedFilesCount},
        {"trashed_bytes_total", stats.trashedBytesTotal},
        {"oldest_trashed_age_seconds", nullableRetentionValue(stats.oldestTrashedAgeSeconds)},
        {"trashed_files_past_retention_count", stats.trashedFilesPastRetentionCount},
        {"trashed_bytes_past_retention", stats.trashedBytesPastRetention},
        {"sync_events_total", stats.syncEventsTotal},
        {"sync_events_past_retention_count", stats.syncEventsPastRetentionCount},
        {"audit_log_entries_total", stats.auditLogEntriesTotal},
        {"oldest_audit_log_age_seconds", nullableRetentionValue(stats.oldestAuditLogAgeSeconds)},
        {"audit_log_entries_past_retention_count", stats.auditLogEntriesPastRetentionCount},
        {"share_access_events_total", stats.shareAccessEventsTotal},
        {"oldest_share_access_event_age_seconds", nullableRetentionValue(stats.oldestShareAccessEventAgeSeconds)},
        {"cache_entries_total", stats.cacheEntriesTotal},
        {"cache_bytes_total", stats.cacheBytesTotal},
        {"cache_eviction_candidates", stats.cacheEvictionCandidates},
        {"cache_entries_expired", stats.cacheEntriesExpired},
        {"checked_at", stats.checkedAt},
    };
}

}
