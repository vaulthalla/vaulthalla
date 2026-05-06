#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct VaultSecurity {
    std::uint32_t vaultId = 0;
    std::string overallStatus = "unknown";
    std::string encryptionStatus = "unknown";

    std::optional<std::uint32_t> currentKeyVersion;
    std::optional<std::uint64_t> keyCreatedAt;
    std::optional<std::uint64_t> keyAgeDays;
    std::uint64_t trashedKeyVersionsCount = 0;

    std::uint64_t fileCount = 0;
    std::uint64_t filesCurrentKeyVersion = 0;
    std::uint64_t filesLegacyKeyVersion = 0;
    std::uint64_t filesUnknownKeyVersion = 0;

    std::uint64_t unauthorizedAccessAttempts24h = 0;
    std::uint64_t rateLimitedAttempts24h = 0;
    std::optional<std::uint64_t> lastDeniedAccessAt;
    std::optional<std::string> lastDeniedAccessReason;
    std::optional<std::uint64_t> lastPermissionChangeAt;
    std::optional<std::uint64_t> lastSharePolicyChangeAt;

    std::string integrityCheckStatus = "not_available";
    std::optional<std::uint64_t> lastIntegrityCheckAt;
    std::optional<std::uint64_t> checksumMismatchCount;

    std::uint64_t checkedAt = 0;
};

void to_json(nlohmann::json& j, const VaultSecurity& security);

}
