#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct VaultShareTopLink {
    std::string shareId;
    std::string label;
    std::string rootPath;
    std::string linkType;
    std::string accessMode;
    std::uint64_t accessCount = 0;
    std::uint64_t downloadCount = 0;
    std::uint64_t uploadCount = 0;
    std::optional<std::uint64_t> lastAccessedAt;
};

struct VaultShareRecentEvent {
    std::uint64_t id = 0;
    std::optional<std::string> shareId;
    std::string eventType;
    std::string status;
    std::optional<std::string> targetPath;
    std::uint64_t bytesTransferred = 0;
    std::optional<std::string> errorCode;
    std::optional<std::string> errorMessage;
    std::uint64_t createdAt = 0;
};

struct VaultShareStats {
    std::uint32_t vaultId = 0;

    std::uint64_t activeLinks = 0;
    std::uint64_t expiredLinks = 0;
    std::uint64_t revokedLinks = 0;
    std::uint64_t linksCreated24h = 0;
    std::uint64_t linksRevoked24h = 0;
    std::uint64_t publicLinks = 0;
    std::uint64_t emailValidatedLinks = 0;

    std::uint64_t downloads24h = 0;
    std::uint64_t uploads24h = 0;
    std::uint64_t deniedAttempts24h = 0;
    std::uint64_t rateLimitedAttempts24h = 0;
    std::uint64_t failedAttempts24h = 0;

    std::vector<VaultShareTopLink> topLinksByAccess;
    std::vector<VaultShareRecentEvent> recentShareEvents;

    std::uint64_t checkedAt = 0;
};

void to_json(nlohmann::json& j, const VaultShareTopLink& link);
void to_json(nlohmann::json& j, const VaultShareRecentEvent& event);
void to_json(nlohmann::json& j, const VaultShareStats& stats);

}
