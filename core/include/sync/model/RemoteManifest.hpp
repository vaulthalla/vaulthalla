#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::fs::model { struct File; }

namespace vh::sync::model::remote_manifest {

inline constexpr const char* INDEX_V1_KEY = ".vaulthalla/index-v1.json";
inline constexpr uint32_t INDEX_V1_VERSION = 1;

struct IndexV1Metadata {
    uint32_t version{};
    uint32_t vault_id{};
    std::time_t generated_at{};
    uint64_t object_count{};
    std::optional<std::string> object_checksum;
};

[[nodiscard]] bool isVaulthallaManifestKey(const std::filesystem::path& key);
[[nodiscard]] std::string buildIndexV1(uint32_t vaultId, const std::vector<std::shared_ptr<fs::model::File>>& files);
[[nodiscard]] IndexV1Metadata inspectIndexV1Metadata(
    const std::string& manifest,
    std::optional<uint32_t> expectedVaultId = std::nullopt);
[[nodiscard]] std::vector<std::shared_ptr<fs::model::File>> parseIndexV1(
    const std::string& manifest,
    std::optional<uint32_t> expectedVaultId = std::nullopt);

}
