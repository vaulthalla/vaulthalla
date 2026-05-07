#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <pqxx/pqxx>

#include "fuse/resolver/Resolved.hpp"

namespace vh::identities {
struct Group;
struct User;
}

namespace vh::storage {
class Manager;
struct Engine;
}

namespace vh::fs {

namespace model {
struct Entry;
struct File;
struct Symlink;
}

struct RenameContext {
    std::filesystem::path from, to;
    std::vector<uint8_t> buffer;
    std::shared_ptr<identities::User> user;
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<model::Entry> entry;
    pqxx::work& txn;
};

struct NewFileContext {
    std::filesystem::path path{}, fuse_path{};
    std::vector<uint8_t> buffer{};
    std::shared_ptr<storage::Engine> engine = nullptr;
    std::shared_ptr<identities::User> user = nullptr;
    std::shared_ptr<identities::Group> group = nullptr;
    mode_t mode = 0644;
    bool overwrite = false;
};

struct MkdirContext {
    std::filesystem::path path{};
    mode_t mode = 0755;
    std::shared_ptr<storage::Engine> engine = nullptr;
    std::shared_ptr<identities::User> user = nullptr;
    std::shared_ptr<identities::Group> group = nullptr;
    std::shared_ptr<model::Entry> parent = nullptr;
    bool failIfExists = false;
};

struct FuseMkdirContext {
    fuse::resolver::Resolved resolved{};
    mode_t mode = 0755;
};

struct FuseCreateFileContext {
    fuse::resolver::Resolved resolved{};
    mode_t mode = 0644;
};

struct FuseCreateSymlinkContext {
    fuse::resolver::Resolved resolved{};
    std::string target{};
};

class Filesystem {
public:
    static void init(const std::shared_ptr<storage::Manager>& manager);
    static bool isReady();
    static void mkVault(const std::filesystem::path& absPath, unsigned int vaultId, mode_t mode = 0755);
    static bool exists(const std::filesystem::path& absPath);

    static int mkdir(const MkdirContext& ctx);
    static std::pair<int, std::shared_ptr<model::Entry>> mkdir(const FuseMkdirContext& ctx);

    static int copy(const std::filesystem::path& from, const std::filesystem::path& to, unsigned int userId, std::shared_ptr<storage::Engine> engine = nullptr);
    static void remove(const std::filesystem::path& path, unsigned int userId);
    static int rename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, const std::shared_ptr<identities::User>& user = nullptr, std::shared_ptr<storage::Engine> engine = nullptr);

    static std::pair<int, std::shared_ptr<model::Entry>> createFile(const FuseCreateFileContext& ctx);
    static std::shared_ptr<model::File> createFile(const NewFileContext& ctx);
    static std::pair<int, std::shared_ptr<model::Symlink>> createSymlink(const FuseCreateSymlinkContext& ctx);

    static bool isPreviewable(const std::string& mimeType);

private:
    inline static std::mutex mutex_;
    inline static std::shared_ptr<storage::Manager> storageManager_ = nullptr;

    static int handleRename(const RenameContext& ctx);

    static bool canFastPath(const std::shared_ptr<model::Entry>& entry, const std::shared_ptr<storage::Engine>& engine);
};

}
