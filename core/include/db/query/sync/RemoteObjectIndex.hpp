#pragma once

#include <cstdint>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::fs::model { struct File; }

namespace vh::db::query::sync {

[[nodiscard]] bool s3SequencerIsNewerOrEqual(
    const std::optional<std::string>& candidate,
    const std::optional<std::string>& current);

struct RemoteIndexSummary {
    uint64_t object_count{};
    std::optional<std::string> source;
    std::optional<std::time_t> indexed_at;
    std::optional<std::string> manifest_etag;
    std::optional<std::time_t> manifest_updated_at;
    std::optional<std::time_t> manifest_generated_at;
    std::optional<uint64_t> manifest_object_count;
    std::optional<std::string> manifest_object_checksum;

    [[nodiscard]] bool hasIndex() const noexcept { return object_count > 0 && indexed_at.has_value(); }
    [[nodiscard]] bool isStale(std::optional<std::chrono::seconds> maxAge, std::time_t now = std::time(nullptr)) const noexcept;
};

class RemoteObjectIndex {
public:
    using FilePtr = std::shared_ptr<vh::fs::model::File>;

    static uint64_t countForVault(uint32_t vaultId);
    static RemoteIndexSummary summaryForVault(uint32_t vaultId, const std::string& manifestKey = ".vaulthalla/index-v1.json");
    static std::vector<FilePtr> listFilesForVault(uint32_t vaultId);
    static void replaceFromListObjects(uint32_t vaultId, const std::vector<FilePtr>& files);
    static void replaceFromManifest(uint32_t vaultId, const std::vector<FilePtr>& files);
    static void replace(uint32_t vaultId, const std::vector<FilePtr>& files, const std::string& source);
    static void upsertFile(uint32_t vaultId, const FilePtr& file, const std::string& source = "manifest");
    static void upsertEventFile(uint32_t vaultId, const FilePtr& file);
    static void deleteKey(uint32_t vaultId, const std::filesystem::path& key);
    static void deleteEventKey(uint32_t vaultId, const std::filesystem::path& key, const std::optional<std::string>& sequencer);
    static std::optional<std::string> getManifestETag(uint32_t vaultId, const std::string& manifestKey);
    static void upsertManifestState(
        uint32_t vaultId,
        const std::string& manifestKey,
        const std::optional<std::string>& etag,
        std::optional<std::time_t> generatedAt = std::nullopt,
        std::optional<uint64_t> objectCount = std::nullopt,
        const std::optional<std::string>& objectChecksum = std::nullopt);
    static void upsertManifestETag(uint32_t vaultId, const std::string& manifestKey, const std::optional<std::string>& etag);
};

}
