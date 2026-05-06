#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct VaultActivityTopUser {
    std::optional<std::uint32_t> userId;
    std::optional<std::string> userName;
    std::uint64_t count = 0;
};

struct VaultActivityTopPath {
    std::string path;
    std::string action;
    std::uint64_t count = 0;
    std::uint64_t bytes = 0;
};

struct VaultActivityEvent {
    std::string source;
    std::string action;
    std::string path;
    std::optional<std::uint32_t> userId;
    std::optional<std::string> userName;
    std::optional<std::string> status;
    std::optional<std::string> error;
    std::uint64_t bytes = 0;
    std::uint64_t occurredAt = 0;
};

struct VaultActivity {
    std::uint32_t vaultId = 0;

    std::optional<std::uint64_t> lastActivityAt;
    std::optional<std::string> lastActivityAction;

    std::uint64_t uploads24h = 0;
    std::uint64_t uploads7d = 0;
    std::uint64_t deletes24h = 0;
    std::uint64_t deletes7d = 0;
    std::uint64_t renames24h = 0;
    std::uint64_t renames7d = 0;
    std::uint64_t moves24h = 0;
    std::uint64_t moves7d = 0;
    std::uint64_t copies24h = 0;
    std::uint64_t copies7d = 0;
    std::uint64_t restores24h = 0;
    std::uint64_t restores7d = 0;

    std::uint64_t bytesAdded24h = 0;
    std::uint64_t bytesRemoved24h = 0;

    std::vector<VaultActivityTopUser> topActiveUsers;
    std::vector<VaultActivityTopPath> topTouchedPaths;
    std::vector<VaultActivityEvent> recentActivity;

    std::uint64_t checkedAt = 0;
};

void to_json(nlohmann::json& j, const VaultActivityTopUser& user);
void to_json(nlohmann::json& j, const VaultActivityTopPath& path);
void to_json(nlohmann::json& j, const VaultActivityEvent& event);
void to_json(nlohmann::json& j, const VaultActivity& activity);

}
