#pragma once

#define FUSE_USE_VERSION 35

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <filesystem>
#include <vector>
#include <fuse3/fuse_lowlevel.h>

namespace vh::stats::model { struct CacheStatsSnapshot; struct CacheStats; }
namespace vh::fs::model { struct Entry; }

namespace vh::fs::cache {

class Registry {
public:
    Registry();

    [[nodiscard]] std::shared_ptr<fs::model::Entry> getEntry(const std::filesystem::path& absPath);
    [[nodiscard]] std::shared_ptr<fs::model::Entry> getEntry(fuse_ino_t ino);
    [[nodiscard]] std::shared_ptr<fs::model::Entry> getEntryById(unsigned int id);

    fuse_ino_t assignInode(const std::filesystem::path& path);
    fuse_ino_t getOrAssignInode(const std::filesystem::path& path);
    fuse_ino_t resolveInode(const std::filesystem::path& absPath);
    std::filesystem::path resolvePath(fuse_ino_t ino);
    void linkPath(const std::filesystem::path& absPath, fuse_ino_t ino);
    void decrementInodeRef(fuse_ino_t ino, uint64_t nlookup);

    void cacheEntry(const std::shared_ptr<fs::model::Entry>& entry, bool isFirstSeeding = false);
    void updateEntry(const std::shared_ptr<fs::model::Entry>& entry);
    [[nodiscard]] bool entryExists(const std::filesystem::path& absPath) const;
    std::shared_ptr<fs::model::Entry> getEntryFromInode(fuse_ino_t ino) const;

    void evictIno(fuse_ino_t ino);
    void evictPath(const std::filesystem::path& path);

    std::vector<std::shared_ptr<fs::model::Entry>> listDir(unsigned int parentId, bool recursive = false) const;

    std::shared_ptr<stats::model::CacheStatsSnapshot> stats() const;

private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<stats::model::CacheStats> stats_;
    fuse_ino_t nextInode_ = 2;
    std::unordered_map<fuse_ino_t, std::filesystem::path> inodeToPath_;
    std::unordered_map<std::filesystem::path, fuse_ino_t> pathToInode_;
    std::unordered_map<fuse_ino_t, std::shared_ptr<fs::model::Entry>> inodeToEntry_;
    std::pmr::unordered_map<std::filesystem::path, std::shared_ptr<fs::model::Entry>> pathToEntry_;
    std::unordered_map<fuse_ino_t, unsigned int> inodeToId_;
    std::unordered_map<unsigned int, std::shared_ptr<fs::model::Entry>> idToEntry_;
    std::unordered_map<unsigned int, unsigned int> childToParent_;

    void initRoot();
    void restoreCache();
};

}
