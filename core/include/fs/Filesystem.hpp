#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <utility>
#include <pqxx/pqxx>

namespace vh::storage {
class Manager;
struct Engine;
}

namespace vh::fs {

namespace model {
struct Entry;
struct File;
}

struct RenameContext {
    std::filesystem::path from, to;
    std::vector<uint8_t> buffer;
    std::optional<unsigned int> userId;
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<model::Entry> entry;
    pqxx::work& txn;
};

struct NewFileContext {
    std::filesystem::path path{}, fuse_path{};
    std::vector<uint8_t> buffer{};
    std::shared_ptr<storage::Engine> engine = nullptr;
    std::optional<unsigned int> userId{}, linux_uid{}, linux_gid{};
    mode_t mode = 0644;
    bool overwrite = false;
};

class Filesystem {
public:
    static void init(const std::shared_ptr<storage::Manager>& manager);
    static bool isReady();
    static void mkVault(const std::filesystem::path& absPath, unsigned int vaultId, mode_t mode = 0755);
    static bool exists(const std::filesystem::path& absPath);

    static int mkdir(const std::filesystem::path& absPath, mode_t mode = 0755, const std::optional<unsigned int>& userId = std::nullopt, std::shared_ptr<storage::Engine> engine = nullptr);

    static int copy(const std::filesystem::path& from, const std::filesystem::path& to, unsigned int userId, std::shared_ptr<storage::Engine> engine = nullptr);
    static void remove(const std::filesystem::path& path, unsigned int userId);
    static int rename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, const std::optional<unsigned int>& userId = std::nullopt, std::shared_ptr<storage::Engine> engine = nullptr);

    static std::pair<int, std::shared_ptr<model::Entry>> createFile(const std::filesystem::path& path, uid_t uid, gid_t gid, mode_t mode = 0644);
    static std::shared_ptr<model::File> createFile(const NewFileContext& ctx);

    static bool isPreviewable(const std::string& mimeType);

private:
    inline static std::mutex mutex_;
    inline static std::shared_ptr<storage::Manager> storageManager_ = nullptr;

    static int handleRename(const RenameContext& ctx);

    static bool canFastPath(const std::shared_ptr<model::Entry>& entry, const std::shared_ptr<storage::Engine>& engine);
};

}
