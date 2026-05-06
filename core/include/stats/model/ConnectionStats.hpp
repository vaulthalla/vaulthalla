#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct ConnectionStats {
    std::string status = "healthy";
    std::uint64_t activeWsSessionsTotal = 0;
    std::uint64_t activeHumanSessions = 0;
    std::uint64_t activeShareSessions = 0;
    std::uint64_t activeSharePendingSessions = 0;
    std::uint64_t activeUnauthenticatedSessions = 0;
    std::optional<std::uint64_t> oldestSessionAgeSeconds;
    std::optional<std::uint64_t> oldestUnauthenticatedSessionAgeSeconds;
    std::uint64_t idleTimeoutMinutes = 0;
    std::uint64_t unauthenticatedTimeoutSeconds = 0;
    std::uint64_t sweepIntervalSeconds = 0;
    std::optional<std::uint64_t> sessionsSwept24h;
    std::optional<std::uint64_t> connectionsOpened24h;
    std::optional<std::uint64_t> connectionsClosed24h;
    std::optional<std::uint64_t> connectionErrors24h;
    std::map<std::string, std::uint64_t> sessionsByMode{
        {"unauthenticated", 0},
        {"human", 0},
        {"share_pending", 0},
        {"share", 0},
    };
    bool topUserAgentsAvailable = false;
    bool topRemoteIpsAvailable = false;
    std::uint64_t checkedAt = 0;

    static ConnectionStats snapshot();
    void finalize();
};

void to_json(nlohmann::json& j, const ConnectionStats& stats);

}
