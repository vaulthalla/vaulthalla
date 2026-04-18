#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace vh::fs::model { struct Entry; struct File; struct Directory; }
namespace pqxx { class result; }

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
};

}
