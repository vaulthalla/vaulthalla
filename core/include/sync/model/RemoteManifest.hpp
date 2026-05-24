#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vh::fs::model { struct File; }

namespace vh::sync::model::remote_manifest {

inline constexpr const char* INDEX_V1_KEY = ".vaulthalla/index-v1.json";
inline constexpr uint32_t INDEX_V1_VERSION = 1;

[[nodiscard]] bool isVaulthallaManifestKey(const std::filesystem::path& key);
[[nodiscard]] std::string buildIndexV1(uint32_t vaultId, const std::vector<std::shared_ptr<fs::model::File>>& files);
[[nodiscard]] std::vector<std::shared_ptr<fs::model::File>> parseIndexV1(const std::string& manifest);

}
