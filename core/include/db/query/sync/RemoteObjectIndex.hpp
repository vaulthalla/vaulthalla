#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::fs::model { struct File; }

namespace vh::db::query::sync {

class RemoteObjectIndex {
public:
    using FilePtr = std::shared_ptr<vh::fs::model::File>;

    static uint64_t countForVault(uint32_t vaultId);
    static std::vector<FilePtr> listFilesForVault(uint32_t vaultId);
    static void replaceFromListObjects(uint32_t vaultId, const std::vector<FilePtr>& files);
    static void replaceFromManifest(uint32_t vaultId, const std::vector<FilePtr>& files);
    static void replace(uint32_t vaultId, const std::vector<FilePtr>& files, const std::string& source);
    static void upsertFile(uint32_t vaultId, const FilePtr& file, const std::string& source = "manifest");
    static void deleteKey(uint32_t vaultId, const std::filesystem::path& key);
    static std::optional<std::string> getManifestETag(uint32_t vaultId, const std::string& manifestKey);
    static void upsertManifestETag(uint32_t vaultId, const std::string& manifestKey, const std::optional<std::string>& etag);
};

}
