#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <optional>

#include <pqxx/pqxx>
#include "fs/Fwd.hpp"

namespace vh::db::query::fs {

class Directory {
    using Dir = vh::fs::model::Directory;
    using DirPtr = std::shared_ptr<Dir>;
    using Dirs = std::vector<DirPtr>;

public:
    Directory() = default;

    static unsigned int upsertDirectory(const DirPtr& directory);

    [[nodiscard]] static bool isDirectory(unsigned int vaultId, const std::filesystem::path& relPath);

    [[nodiscard]] static bool directoryExists(unsigned int vaultId, const std::filesystem::path& relPath);

    static void moveDirectory(const DirPtr& directory, const std::filesystem::path& newPath, unsigned int userId);

    static DirPtr getDirectoryByPath(unsigned int vaultId, const std::filesystem::path& relPath);

    [[nodiscard]] static std::optional<unsigned int> getDirectoryIdByPath(unsigned int vaultId, const std::filesystem::path& path);

    [[nodiscard]] static unsigned int getRootDirectoryId(unsigned int vaultId);

    static Dirs listDirectoriesInDir(unsigned int parentId, bool recursive = false);

    [[nodiscard]] static pqxx::result collectParentStats(unsigned int parentId);

    static void deleteEmptyDirectory(unsigned int id);

    [[nodiscard]] static bool isDirectoryEmpty(unsigned int id);

    // Directory size_bytes / file_count / subdirectory_count are subtree totals kept on every ancestor (#158).
    struct SubtreeTotals {
        int64_t size_bytes = 0;
        int64_t files = 0;
        int64_t subdirs = 0;
    };

    // What entry `id` contributes to each ancestor: a directory its own totals plus itself, a file or symlink its
    // size and one entry. nullopt when the entry doesn't exist.
    [[nodiscard]] static std::optional<SubtreeTotals> subtreeTotalsOf(pqxx::work& txn, unsigned int id);

    [[nodiscard]] static std::optional<unsigned int> parentIdOf(pqxx::work& txn, unsigned int id);

    // The ids of entry `id`'s ancestors, nearest first.
    [[nodiscard]] static std::vector<unsigned int> ancestorsOf(unsigned int id);

    // Moves `totals` off oldParentId and its ancestors onto newParentId and its ancestors; ancestors both chains
    // share are left alone. A no-op when the parents are the same.
    static void shiftSubtreeTotals(pqxx::work& txn, std::optional<unsigned int> oldParentId,
                                   std::optional<unsigned int> newParentId, const SubtreeTotals& totals);

    // Deletes directory `id` and everything still under it, taking its totals off its ancestors. A no-op when the
    // directory is already gone.
    static void deleteDirectoryTree(unsigned int id);
};

}
