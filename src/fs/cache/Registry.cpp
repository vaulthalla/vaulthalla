#include "fs/cache/Registry.hpp"

#include "fs/model/Entry.hpp"
#include "fs/model/Directory.hpp"
#include "db/query/fs/Entry.hpp"
#include "db/query/fs/Directory.hpp"
#include "log/Registry.hpp"
#include "crypto/id/Generator.hpp"
#include "stats/model/CacheStats.hpp"
#include "fs/model/Path.hpp"

#include <unordered_set>
#include <mutex>
#include <limits>
#include <chrono>

using namespace vh::fs::model;
using namespace vh::stats::model;
using namespace std::chrono;

namespace {

// “Represented bytes” for cache working-set tracking.
// This is NOT RAM usage — it’s how much content the cached entries represent.
uint64_t safeSizeBytes(const std::shared_ptr<Entry>& e) noexcept {
    if (!e) return 0;
    const auto s = static_cast<unsigned long long>(e->size_bytes);
    return (s > std::numeric_limits<uint64_t>::max())
        ? std::numeric_limits<uint64_t>::max()
        : static_cast<uint64_t>(s);
}

uint64_t addClamp(uint64_t a, uint64_t b) noexcept {
    constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
    return (max - a < b) ? max : (a + b);
}

uint64_t subClamp(uint64_t a, uint64_t b) noexcept {
    return (a >= b) ? (a - b) : 0;
}

} // namespace

namespace vh::fs::cache {

Registry::Registry() {
    stats_ = std::make_shared<CacheStats>();

    // Seed hard root mapping for FUSE
    inodeToPath_[FUSE_ROOT_ID] = "/";
    pathToInode_["/"] = FUSE_ROOT_ID;

    nextInode_ = db::query::fs::Entry::getNextInode();

    initRoot();
    restoreCache();

    log::Registry::storage()->info("[FSCache] Initialized with next inode: {}", nextInode_);
}

void Registry::initRoot() {
    const auto rootEntry = db::query::fs::Entry::getRootEntry();
    if (!rootEntry) {
        log::Registry::storage()->error("[FSCache] Root entry not found in the database");
        throw std::runtime_error("Root entry not found in the database");
    }

    if (!rootEntry->inode) {
        rootEntry->inode = std::make_optional(getOrAssignInode("/"));
        db::query::fs::Entry::updateFSEntry(rootEntry);
    } else if (rootEntry->inode != FUSE_ROOT_ID) {
        log::Registry::storage()->warn("[FSCache] Root entry inode is not FUSE_ROOT_ID, resetting to 1");
        rootEntry->inode = FUSE_ROOT_ID;
        db::query::fs::Entry::updateFSEntry(rootEntry);
    }

    cacheEntry(rootEntry);
}

void Registry::restoreCache() {
    const auto rootEntry = getEntry(FUSE_ROOT_ID);
    if (!rootEntry) throw std::runtime_error("[FSCache] Root entry not found, cannot restore cache");

    for (const auto& entry : db::query::fs::Entry::listDir(rootEntry->id, true)) {
        if (!entry) {
            log::Registry::storage()->warn("[FSCache] Null entry found in directory listing for root");
            continue;
        }

        if (entry->inode) cacheEntry(entry, true /*isFirstSeeding*/);
        else {
            entry->inode = std::make_optional(getOrAssignInode(entry->fuse_path));
            db::query::fs::Entry::updateFSEntry(entry);
            cacheEntry(entry, true /*isFirstSeeding*/);
        }
    }
}

std::shared_ptr<Entry> Registry::getEntry(const std::filesystem::path& absPath) {
    const auto path = makeAbsolute(absPath);
    log::Registry::storage()->debug("[FSCache] Retrieving entry for path: {}", path.string());
    
    {
        std::shared_lock lock(mutex_);
        if (const auto it = pathToEntry_.find(path); it != pathToEntry_.end()) {
            stats_->record_hit();
            return it->second;
        }
    }

    stats_->record_miss();
    ScopedOpTimer timer(stats_.get());

    try {
        const auto ino = getOrAssignInode(path);
        const auto entry = db::query::fs::Entry::getFSEntryByInode(ino);

        if (entry) {
            if (entry->inode) cacheEntry(entry);
            else {
                entry->inode = ino;
                cacheEntry(entry);
            }
        } else log::Registry::storage()->debug("[FSCache] No entry found for path: {}", path.string());

        return entry;
    } catch (const std::exception& e) {
        log::Registry::storage()->error("[FSCache] Error retrieving entry for path {}: {}", path.string(), e.what());
        return nullptr;
    }
}

std::shared_ptr<Entry> Registry::getEntry(const fuse_ino_t ino) {
    log::Registry::storage()->debug("[FSCache] Retrieving entry for inode: {}", ino);

    {
        std::shared_lock lock(mutex_);
        if (const auto it = inodeToEntry_.find(ino); it != inodeToEntry_.end()) {
            stats_->record_hit();
            return it->second;
        }
    }

    stats_->record_miss();
    ScopedOpTimer timer(stats_.get());

    auto entry = db::query::fs::Entry::getFSEntryByInode(ino);
    if (entry) cacheEntry(entry);
    else log::Registry::storage()->warn("[FSCache] No entry found for inode: {}", ino);
    return entry;
}

std::shared_ptr<Entry> Registry::getEntryById(unsigned int id) {
    std::shared_lock lock(mutex_);
    if (const auto it = idToEntry_.find(id); it != idToEntry_.end()) {
        stats_->record_hit();
        return it->second;
    }

    stats_->record_miss();
    ScopedOpTimer timer(stats_.get());

    std::shared_ptr<Entry> entry = nullptr;
    log::Registry::storage()->warn("[FSCache] No entry found for ID: {}", id);
    entry = db::query::fs::Entry::getFSEntryById(id);
    return entry;
}

fuse_ino_t Registry::resolveInode(const std::filesystem::path& absPath) {
    // Try hot map first
    {
        std::shared_lock lock(mutex_);
        if (const auto it = pathToInode_.find(absPath); it != pathToInode_.end()) return it->second;
    }

    const auto entry = getEntry(absPath);
    if (!entry) {
        log::Registry::storage()->warn("[FSCache] No entry found for path: {}", absPath.string());
        return FUSE_ROOT_ID;
    }

    const fuse_ino_t ino = (entry->inode ? *entry->inode : getOrAssignInode(absPath));
    if (ino == FUSE_ROOT_ID) {
        log::Registry::storage()->error("[FSCache] Failed to resolve inode for path: {}", absPath.string());
        return FUSE_ROOT_ID;
    }

    linkPath(absPath, ino);
    return ino;
}

fuse_ino_t Registry::assignInode(const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    if (const auto it = pathToInode_.find(path); it != pathToInode_.end()) return it->second;

    const fuse_ino_t ino = nextInode_++;
    inodeToPath_[ino] = path;
    pathToInode_[path] = ino;
    return ino;
}

fuse_ino_t Registry::getOrAssignInode(const std::filesystem::path& path) {
    std::scoped_lock lock(mutex_);
    if (const auto it = pathToInode_.find(path); it != pathToInode_.end()) return it->second;

    const fuse_ino_t ino = nextInode_++;
    pathToInode_[path] = ino;
    inodeToPath_[ino] = path;
    return ino;
}

std::filesystem::path Registry::resolvePath(fuse_ino_t ino) {
    std::shared_lock lock(mutex_);
    if (!inodeToPath_.contains(ino)) throw std::runtime_error("[FSCache] Inode not found: " + std::to_string(ino));
    return inodeToPath_.at(ino);
}

void Registry::linkPath(const std::filesystem::path& absPath, const fuse_ino_t ino) {
    std::unique_lock lock(mutex_);
    if (pathToInode_.contains(absPath)) {
        log::Registry::storage()->debug("[FSCache] Path {} already linked to inode {}", absPath.string(), ino);
        return;
    }
    pathToInode_[absPath] = ino;
    inodeToPath_[ino] = absPath;
}

void Registry::decrementInodeRef(const fuse_ino_t ino, const uint64_t nlookup) {
    if (nlookup > 1) return;
    evictIno(ino);
}

bool Registry::entryExists(const std::filesystem::path& absPath) const {
    std::shared_lock lock(mutex_);
    return pathToEntry_.contains(absPath);
}

std::shared_ptr<Entry> Registry::getEntryFromInode(const fuse_ino_t ino) const {
    std::shared_lock lock(mutex_);
    if (const auto it = inodeToEntry_.find(ino); it != inodeToEntry_.end()) return it->second;
    log::Registry::storage()->warn("[FSCache] No entry found for inode: {}", ino);
    return nullptr;
}

void Registry::cacheEntry(const std::shared_ptr<Entry>& entry, const bool isFirstSeeding) {
    if (!entry || !entry->inode) throw std::invalid_argument("Entry or inode is null");

    log::Registry::storage()->debug("[FSCache] Caching entry: {} with inode {}", entry->fuse_path.string(), *entry->inode);

    // Preflight: reconstruct fuse/backing paths when needed
    if (*entry->inode != FUSE_ROOT_ID) {
        if (!entry->vault_id) {
            log::Registry::storage()->error("[FSCache] Entry {} has no vault_id, cannot cache", entry->id);
            throw std::runtime_error("Entry has no vault_id");
        }

        if (entry->fuse_path.empty() || entry->backing_path.empty()) {
            if (!entry->parent_id) {
                log::Registry::storage()->error("[FSCache] Entry {} has no parent_id, cannot cache", entry->id);
                throw std::runtime_error("Entry has no parent_id");
            }

            if (const auto parent = getEntryById(*entry->parent_id)) {
                entry->fuse_path = parent->fuse_path / entry->name;
                entry->backing_path = parent->backing_path / entry->base32_alias;
            } else {
                log::Registry::storage()->error("[FSCache] No parent entry found for entry {}", entry->id);
                throw std::runtime_error("No parent entry found");
            }
        }
    }

    const pqxx::result parentStats =
        (!isFirstSeeding && entry->parent_id && !entry->isDirectory())
            ? db::query::fs::Directory::collectParentStats(*entry->parent_id)
            : pqxx::result{};

    std::unique_lock lock(mutex_);

    // --- Used-bytes accounting (incremental, no rescans) ---
    const fuse_ino_t ino = *entry->inode;

    bool existedAlready = false;
    uint64_t oldSize = 0;

    // Prefer inode match for “old” instance
    if (const auto it = inodeToEntry_.find(ino); it != inodeToEntry_.end() && it->second) {
        existedAlready = true;
        oldSize = safeSizeBytes(it->second);
    } else if (const auto pit = pathToEntry_.find(entry->fuse_path); pit != pathToEntry_.end() && pit->second) {
        existedAlready = true;
        oldSize = safeSizeBytes(pit->second);
    } else if (const auto iit = idToEntry_.find(entry->id); iit != idToEntry_.end() && iit->second) {
        existedAlready = true;
        oldSize = safeSizeBytes(iit->second);
    }

    const uint64_t newSize = safeSizeBytes(entry);

    auto insert = [&](const std::shared_ptr<Entry>& e) {
        if (!e) {
            log::Registry::fs()->error("[FSCache] Attempted to cache a null entry");
            return;
        }

        if (!e->inode) {
            log::Registry::fs()->error("[FSCache] Entry {} has no inode, cannot cache", e->id);
            throw std::runtime_error("Entry has no inode");
        }

        inodeToPath_[*e->inode] = e->fuse_path;
        inodeToEntry_[*e->inode] = e;
        pathToInode_[e->fuse_path] = *e->inode;
        pathToEntry_[e->fuse_path] = e;
        inodeToId_[*e->inode] = e->id;
        idToEntry_[e->id] = e;
        if (e->parent_id) childToParent_[e->id] = *e->parent_id;
    };

    insert(entry);

    // Update upstream directory stats (your existing behavior)
    if (!parentStats.empty()) {
        for (const auto& s : parentStats) {
            const auto id = s["id"].as<unsigned int>();
            if (idToEntry_.contains(id)) {
                const auto dir = std::static_pointer_cast<Directory>(idToEntry_[id]);
                dir->size_bytes = s["size_bytes"].as<uintmax_t>();
                dir->file_count = s["file_count"].as<unsigned int>();
                dir->subdirectory_count = s["subdirectory_count"].as<unsigned int>();
            } else {
                insert(db::query::fs::Entry::getFSEntryById(id));
            }
        }
    }

    // Apply bytes delta after insert + upstream updates
    const uint64_t curUsed = stats_->snapshot().used_bytes;
    uint64_t updatedUsed = curUsed;

    if (!existedAlready) {
        updatedUsed = addClamp(curUsed, newSize);
        stats_->record_insert();
    } else {
        if (newSize >= oldSize) updatedUsed = addClamp(curUsed, (newSize - oldSize));
        else updatedUsed = subClamp(curUsed, (oldSize - newSize));
    }

    stats_->set_used(updatedUsed);

    log::Registry::fs()->info("[FSCache] Cached entry: {} with inode {}", entry->fuse_path.string(), *entry->inode);
}

void Registry::updateEntry(const std::shared_ptr<Entry>& entry) {
    log::Registry::fs()->debug("[FSCache] Updating entry: {} with inode {}",
                             entry->fuse_path.string(), entry->inode ? *entry->inode : 0);

    db::query::fs::Entry::updateFSEntry(entry);
    cacheEntry(entry);

    log::Registry::fs()->debug("[FSCache] Updated entry: {} with inode {}",
                             entry->fuse_path.string(), entry->inode ? *entry->inode : 0);
}

void Registry::evictIno(fuse_ino_t ino) {
    std::unique_lock lock(mutex_);

    if (!inodeToPath_.contains(ino) || !inodeToId_.contains(ino)) {
        log::Registry::fs()->debug("[FSCache] Attempted to destroy references for non-existent inode: {}", ino);
        return;
    }

    const auto path = inodeToPath_.at(ino);
    const auto id = inodeToId_.at(ino);

    // Capture removed size before erasing
    uint64_t removedSize = 0;
    if (const auto it = inodeToEntry_.find(ino); it != inodeToEntry_.end()) removedSize = safeSizeBytes(it->second);
    else if (const auto it2 = pathToEntry_.find(path); it2 != pathToEntry_.end()) removedSize = safeSizeBytes(it2->second);
    else if (const auto it3 = idToEntry_.find(id); it3 != idToEntry_.end()) removedSize = safeSizeBytes(it3->second);

    inodeToPath_.erase(ino);
    pathToInode_.erase(path);
    inodeToEntry_.erase(ino);
    pathToEntry_.erase(path);
    inodeToId_.erase(ino);
    idToEntry_.erase(id);

    childToParent_.erase(id);

    // Update used bytes (bounded)
    const uint64_t curUsed = stats_->snapshot().used_bytes;
    stats_->set_used(subClamp(curUsed, removedSize));

    stats_->record_eviction();
}

void Registry::evictPath(const std::filesystem::path& path) {
    fuse_ino_t ino;

    {
        std::unique_lock lock(mutex_);
        if (!pathToInode_.contains(path)) {
            log::Registry::fs()->debug("[FSCache] Attempted to evict non-existent path: {}", path.string());
            return;
        }
        ino = pathToInode_[path];
    }

    stats_->record_invalidation();
    evictIno(ino);
}

std::vector<std::shared_ptr<Entry>> Registry::listDir(const unsigned int parentId, const bool recursive) const {
    const auto parent = db::query::fs::Entry::getFSEntryById(parentId);
    if (!parent->isDirectory()) throw std::runtime_error("Parent ID is not a directory");

    const auto parentDir = std::static_pointer_cast<Directory>(parent);

    unsigned int numEntries = parentDir->file_count + parentDir->subdirectory_count;
    if (!recursive) {
        for (const auto& subdir : db::query::fs::Directory::listDirectoriesInDir(parentId))
            numEntries -= subdir->file_count + subdir->subdirectory_count;
    }

    std::vector<std::shared_ptr<Entry>> entries;
    entries.reserve(childToParent_.size()); // cheap upper bound

    auto append_children = [&](const unsigned int pid) {
        for (const auto& [childId, pId] : childToParent_) {
            if (pId == pid) {
                if (const auto it = idToEntry_.find(childId); it != idToEntry_.end()) {
                    entries.push_back(it->second);
                }
            }
        }
    };

    append_children(parentId);

    if (recursive) {
        std::unordered_set<unsigned int> visited;
        visited.reserve(childToParent_.size());

        for (const auto& e : entries) {
            if (!e) continue;
            const unsigned int eid = e->id;
            if (!visited.insert(eid).second) continue;
            append_children(eid);
        }
    }

    if (entries.size() != numEntries) {
        log::Registry::fs()->warn("[FSCache] Expected {} entries, but found {}", numEntries, entries.size());
        const auto expected = db::query::fs::Entry::listDir(parentId, recursive);
        if (expected.size() != numEntries) {
            if (expected.size() == entries.size())
                log::Registry::fs()->warn("Computed number of entries mismatch with actual");

            throw std::runtime_error(fmt::format(
                "[FSCache] Inconsistent entry count for parent ID {}: expected: {}, found {}, actual: {}",
                parentId, numEntries, entries.size(), expected.size()));
        }
        return expected;
    }

    return entries;
}

std::shared_ptr<CacheStatsSnapshot> Registry::stats() const {
    // Snapshot should be atomics-only; safe without locking FSCache maps.
    return std::make_shared<CacheStatsSnapshot>(stats_->snapshot());
}

}
