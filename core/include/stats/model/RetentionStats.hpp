#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct RetentionStats {
    std::optional<std::uint32_t> vaultId;
    std::string scope = "system";
    std::string cleanupStatus = "unknown";

    std::uint64_t trashRetentionDays = 0;
    std::uint64_t syncEventRetentionDays = 0;
    std::uint64_t syncEventMaxEntries = 0;
    std::uint64_t auditLogRetentionDays = 0;
    std::uint64_t cacheExpiryDays = 0;
    std::uint64_t cacheMaxSizeBytes = 0;

    std::uint64_t trashedFilesCount = 0;
    std::uint64_t trashedBytesTotal = 0;
    std::optional<std::uint64_t> oldestTrashedAgeSeconds;
    std::uint64_t trashedFilesPastRetentionCount = 0;
    std::uint64_t trashedBytesPastRetention = 0;

    std::uint64_t syncEventsTotal = 0;
    std::uint64_t syncEventsPastRetentionCount = 0;

    std::uint64_t auditLogEntriesTotal = 0;
    std::optional<std::uint64_t> oldestAuditLogAgeSeconds;
    std::uint64_t auditLogEntriesPastRetentionCount = 0;

    std::uint64_t shareAccessEventsTotal = 0;
    std::optional<std::uint64_t> oldestShareAccessEventAgeSeconds;

    std::uint64_t cacheEntriesTotal = 0;
    std::uint64_t cacheBytesTotal = 0;
    std::uint64_t cacheEvictionCandidates = 0;
    std::uint64_t cacheEntriesExpired = 0;

    std::uint64_t checkedAt = 0;

    void finalize();
};

void to_json(nlohmann::json& j, const RetentionStats& stats);

}
