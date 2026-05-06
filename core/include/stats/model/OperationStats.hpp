#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct RecentOperationError {
    std::string source;
    std::string operation;
    std::string status;
    std::string target;
    std::string path;
    std::optional<std::string> error;
    std::optional<std::uint64_t> occurredAt;
};

struct OperationStats {
    std::optional<std::uint32_t> vaultId;
    std::string scope = "system";
    std::string overallStatus = "healthy";
    std::uint64_t staleThresholdSeconds = 900;

    std::uint64_t pendingOperations = 0;
    std::uint64_t inProgressOperations = 0;
    std::uint64_t failedOperations24h = 0;
    std::uint64_t cancelledOperations24h = 0;
    std::uint64_t stalledOperations = 0;
    std::optional<std::uint64_t> oldestPendingOperationAgeSeconds;
    std::optional<std::uint64_t> oldestInProgressOperationAgeSeconds;

    std::map<std::string, std::uint64_t> operationsByType{
        {"move", 0},
        {"copy", 0},
        {"rename", 0},
    };
    std::map<std::string, std::uint64_t> operationsByStatus{
        {"pending", 0},
        {"in_progress", 0},
        {"success", 0},
        {"error", 0},
        {"cancelled", 0},
    };

    std::uint64_t activeShareUploads = 0;
    std::uint64_t stalledShareUploads = 0;
    std::uint64_t failedShareUploads24h = 0;
    std::uint64_t uploadBytesExpectedActive = 0;
    std::uint64_t uploadBytesReceivedActive = 0;
    std::optional<std::uint64_t> oldestActiveUploadAgeSeconds;

    std::vector<RecentOperationError> recentOperationErrors;
    std::uint64_t checkedAt = 0;

    void finalize();
};

void to_json(nlohmann::json& j, const RecentOperationError& error);
void to_json(nlohmann::json& j, const OperationStats& stats);

}
