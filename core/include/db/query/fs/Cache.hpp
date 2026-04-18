#pragma once

#include "fs/cache/Record.hpp"

#include <memory>
#include <filesystem>
#include <vector>
#include <optional>

namespace vh::db::query::fs {

class Cache {
    using R = vh::fs::cache::Record;
    using RecordPtr = std::shared_ptr<R>;
    using Records = std::vector<RecordPtr>;

public:
    Cache() = default;

    static void addCacheIndex(const RecordPtr& index);
    static void upsertCacheIndex(const RecordPtr& index);
    static void updateCacheIndex(const RecordPtr& index);
    static void deleteCacheIndex(unsigned int indexId);
    static void deleteCacheIndex(unsigned int vaultId, const std::filesystem::path& relPath);
    static RecordPtr getCacheIndex(unsigned int indexId);
    static RecordPtr getCacheIndexByPath(unsigned int vaultId, const std::filesystem::path& path);
    static Records listCacheIndices(unsigned int vaultId, const std::filesystem::path& relPath = {}, bool recursive = false);
    static Records listCacheIndicesByFile(unsigned int fileId);
    [[nodiscard]] static Records listCacheIndicesByType(unsigned int vaultId, const R::Type& type);
    [[nodiscard]] static Records nLargestCacheIndices(unsigned int n, unsigned int vaultId, const std::filesystem::path& relPath, bool recursive = false);
    [[nodiscard]] static Records nLargestCacheIndicesByType(unsigned int n, unsigned int vaultId, const R::Type& type);

    [[nodiscard]] static unsigned int countCacheIndices(unsigned int vaultId, const std::optional<R::Type>& type = std::nullopt);

    [[nodiscard]] static bool cacheIndexExists(unsigned int vaultId, const std::filesystem::path& relPath);
};

}
