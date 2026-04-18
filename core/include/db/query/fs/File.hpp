#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

namespace vh::fs::model {
struct Entry;
struct File;
struct Directory;
namespace file { struct Trashed; }
namespace stats { struct Extension; }
}

namespace vh::db::query::fs {

class File {
    using F = vh::fs::model::File;
    using T = vh::fs::model::file::Trashed;
    using FilePtr = std::shared_ptr<F>;
    using TrashedFilePtr = std::shared_ptr<T>;
    using EncryptionPair = std::optional<std::pair<std::string, unsigned int>>;

public:
    File() = default;

    static unsigned int upsertFile(const FilePtr& file);

    static void updateFile(const FilePtr& file);

    static void markTrashedFileDeleted(unsigned int id);

    static void deleteFile(unsigned int userId, const FilePtr& file);

    [[nodiscard]] static std::string getMimeType(unsigned int vaultId, const std::filesystem::path& relPath);

    [[nodiscard]] static bool isFile(unsigned int vaultId, const std::filesystem::path& relPath);

    static FilePtr getFileById(unsigned int id);

    static FilePtr getFileByPath(unsigned int vaultId, const std::filesystem::path& relPath);

    static void moveFile(const FilePtr& file, const std::filesystem::path& newPath, unsigned int userId);

    static std::vector<FilePtr> listFilesInDir(unsigned int vaultId, const std::filesystem::path& path = {"/"}, bool recursive = true);

    static std::vector<TrashedFilePtr> listTrashedFiles(unsigned int vaultId);

    static void markFileAsTrashed(unsigned int userId, unsigned int vaultId, const std::filesystem::path& relPath, bool isFuseCall = false);

    static void markFileAsTrashed(unsigned int userId, unsigned int fsId, bool isFuseCall = false);

    static void updateParentStatsAndCleanEmptyDirs(pqxx::work& txn,
                                               std::optional<unsigned int> parentId,
                                               unsigned int sizeBytes,
                                               bool isFuseCall = false);

    [[nodiscard]] static EncryptionPair getEncryptionIVAndVersion(unsigned int vaultId, const std::filesystem::path& relPath);

    static void setEncryptionIVAndVersion(const FilePtr& f);

    static std::vector<FilePtr> getFilesOlderThanKeyVersion(unsigned int vaultId, unsigned int keyVersion);

    [[nodiscard]] static std::string getContentHash(unsigned int vaultId, const std::filesystem::path& relPath);

    static FilePtr getLargestFile(unsigned int vaultId);

    static std::vector<FilePtr> getNLargestFiles(unsigned int vaultId, unsigned int n = 1);

    static std::vector<FilePtr> getAllFiles(unsigned int vaultId);

    static std::vector<vh::fs::model::stats::Extension> getTopExtensionsBySize(unsigned int vaultId, unsigned int limit = 10);
};

}
