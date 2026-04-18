#include "storage/Engine.hpp"
#include "config/Registry.hpp"
#include "vault/model/Vault.hpp"
#include "sync/model/Operation.hpp"
#include "fs/model/Path.hpp"
#include "fs/metadata/Magic.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/sync/Policy.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/sync/Event.hpp"
#include "vault/EncryptionManager.hpp"
#include "fs/Filesystem.hpp"
#include "sync/model/Policy.hpp"
#include "fs/ops/file.hpp"
#include "log/Registry.hpp"
#include "sync/model/Event.hpp"
#include "fs/model/file/Trashed.hpp"
#include "fs/model/File.hpp"

using namespace vh::fs::model;
using namespace vh::fs;
using namespace vh::crypto;
using namespace vh::config;
using namespace vh::storage;
using namespace vh::fs::ops;
using namespace std::chrono;
using namespace vh::fs::metadata;

namespace vh::storage {
    Engine::Engine(const std::shared_ptr<vault::model::Vault> &vault)
        : vault(vault),
          sync(db::query::sync::Policy::getSync(vault->id)),
          paths(std::make_shared<Path>(makeAbsolute(to_snake_case(vault->name)), vault->mount_point)),
          encryptionManager(std::make_shared<vault::EncryptionManager>(vault->id)) {
        if (!db::query::vault::Vault::vaultRootExists(vault->id)) Filesystem::mkVault(
            paths->absRelToRoot(paths->vaultRoot, PathType::FUSE_ROOT), vault->id);
        if (!fs::exists(paths->cacheRoot)) fs::create_directories(paths->cacheRoot);
    }

    void Engine::newSyncEvent(const uint8_t trigger) {
        if (latestSyncEvent) {
            db::query::sync::Event::upsert(latestSyncEvent);
            if (latestSyncEvent->status != sync::model::Event::Status::SUCCESS && latestSyncEvent->status !=
                sync::model::Event::Status::CANCELLED) {
                log::Registry::storage()->warn("[StorageEngine] Previous sync event failed with status: {}",
                                               std::string(sync::model::Event::toString(latestSyncEvent->status)));
                return;
            }
        }

        latestSyncEvent = std::make_shared<sync::model::Event>();
        latestSyncEvent->vault_id = vault->id;
        latestSyncEvent->status = sync::model::Event::Status::PENDING;
        latestSyncEvent->trigger = static_cast<sync::model::Event::Trigger>(trigger);
        latestSyncEvent->timestamp_begin = system_clock::to_time_t(system_clock::now());
        latestSyncEvent->config_hash = sync->config_hash;
        db::query::sync::Event::create(latestSyncEvent);
    }

    void Engine::saveSyncEvent() const {
        db::query::sync::Event::upsert(latestSyncEvent);
    }

    bool Engine::isDirectory(const fs::path &rel_path) const {
        return db::query::fs::Directory::isDirectory(vault->id, rel_path);
    }

    bool Engine::isFile(const fs::path &rel_path) const {
        return db::query::fs::File::isFile(vault->id, rel_path);
    }

    std::vector<uint8_t> Engine::decrypt(const std::shared_ptr<File> &f) const {
        const auto context = db::query::fs::File::getEncryptionIVAndVersion(vault->id, f->path);
        if (!context) throw std::runtime_error("No encryption IV found for file: " + f->path.string());
        const auto &[iv_b64, key_version] = *context;
        const auto payload = readFileToVector(f->backing_path);
        if (payload.empty()) throw std::runtime_error("File is empty: " + f->backing_path.string());
        return encryptionManager->decrypt(payload, iv_b64, key_version);
    }

    std::vector<uint8_t> Engine::decrypt(const std::shared_ptr<File> &f, const std::vector<uint8_t> &payload) const {
        if (!f) throw std::invalid_argument("Invalid file for decryption");
        if (payload.empty()) throw std::invalid_argument("Payload for decryption cannot be empty");
        if (f->encryption_iv.empty()) throw std::invalid_argument("File is not encrypted: " + f->path.string());
        const auto context = db::query::fs::File::getEncryptionIVAndVersion(vault->id, f->path);
        if (!context) throw std::runtime_error("No encryption IV found for file: " + f->path.string());
        const auto &[iv_b64, key_version] = *context;
        return encryptionManager->decrypt(payload, iv_b64, key_version);
    }

    std::vector<uint8_t> Engine::decrypt(const unsigned int vaultId, const fs::path &relPath,
                                         const std::vector<uint8_t> &payload) const {
        const auto context = db::query::fs::File::getEncryptionIVAndVersion(vaultId, relPath);
        if (!context) throw std::runtime_error("No encryption IV found for file: " + relPath.string());
        const auto &[iv_b64, key_version] = *context;
        return encryptionManager->decrypt(payload, iv_b64, key_version);
    }

    uintmax_t Engine::getDirectorySize(const fs::path &path) {
        uintmax_t total = 0;
        for (auto &p: fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied))
            if (fs::is_regular_file(p.status())) total += fs::file_size(p);
        return total;
    }

    uintmax_t Engine::getVaultSize() const { return getDirectorySize(paths->backingRoot); }
    uintmax_t Engine::getCacheSize() const { return getDirectorySize(paths->cacheRoot); }
    uintmax_t Engine::getVaultAndCacheTotalSize() const { return getVaultSize() + getCacheSize(); }
    uintmax_t Engine::freeSpace() const { return vault->quota - getVaultAndCacheTotalSize() - MIN_FREE_SPACE; }

    void Engine::purgeThumbnails(const fs::path &rel_path) const {
        for (const auto &size: Registry::get().caching.thumbnails.sizes)
            if (const auto thumbnailPath = paths->absPath(rel_path, PathType::THUMBNAIL_ROOT) / std::to_string(size);
                fs::exists(thumbnailPath))
                fs::remove(thumbnailPath);
    }

    void Engine::moveThumbnails(const std::filesystem::path &from, const std::filesystem::path &to) const {
        for (const auto &size: Registry::get().caching.thumbnails.sizes) {
            auto fromPath = paths->absPath(from, PathType::THUMBNAIL_ROOT) / std::to_string(size);
            auto toPath = paths->absPath(to, PathType::THUMBNAIL_ROOT) / std::to_string(size);

            if (fromPath.extension() != ".jpg" && fromPath.extension() != ".jpeg") {
                fromPath += ".jpg";
                toPath += ".jpg";
            }

            if (!fs::exists(fromPath)) {
                log::Registry::storage()->warn("[StorageEngine] Thumbnail does not exist: {}", fromPath.string());
                continue;
            }

            if (const auto err = Filesystem::mkdir(toPath.parent_path()); err)
                throw std::runtime_error("Failed to create thumbnail directory: " + toPath.parent_path().string() + " Error: " + std::to_string(err));

            fs::rename(fromPath, toPath);
        }
    }

    void Engine::copyThumbnails(const std::filesystem::path &from, const std::filesystem::path &to) const {
        for (const auto &size: Registry::get().caching.thumbnails.sizes) {
            auto fromPath = paths->absPath(from, PathType::THUMBNAIL_ROOT) / std::to_string(size);
            auto toPath = paths->absPath(to, PathType::THUMBNAIL_ROOT) / std::to_string(size);

            if (fromPath.extension() != ".jpg" && fromPath.extension() != ".jpeg") {
                fromPath += ".jpg";
                toPath += ".jpg";
            }

            if (!fs::exists(fromPath)) {
                log::Registry::storage()->warn("[StorageEngine] Thumbnail does not exist: {}", fromPath.string());
                continue;
            }

            if (const auto err = Filesystem::mkdir(toPath.parent_path()); err)
                throw std::runtime_error("Failed to create thumbnail directory: " + toPath.parent_path().string() + " Error: " + std::to_string(err));

            fs::copy_file(fromPath, toPath, fs::copy_options::overwrite_existing);
        }
    }

    void Engine::mkdir(const fs::path &relPath, const unsigned int userId) {
        if (const auto err = Filesystem::mkdir(paths->absRelToAbsRel(relPath, PathType::VAULT_ROOT, PathType::FUSE_ROOT), 0755, userId,
                          shared_from_this()); err)
            throw std::runtime_error("Failed to create directory: " + relPath.string());
    }

    void Engine::move(const fs::path &from, const fs::path &to, const unsigned int userId) {
        if (const auto err = Filesystem::rename(paths->absRelToAbsRel(from, PathType::VAULT_ROOT, PathType::FUSE_ROOT),
                           paths->absRelToAbsRel(to, PathType::VAULT_ROOT, PathType::FUSE_ROOT), userId,
                           shared_from_this()); err)
            throw std::runtime_error("Failed to move directory: " + from.string() + " to " + to.string());
    }

    void Engine::rename(const fs::path &from, const fs::path &to, const unsigned int userId) {
        if (const auto err = Filesystem::rename(paths->absRelToAbsRel(from, PathType::VAULT_ROOT, PathType::FUSE_ROOT),
                           paths->absRelToAbsRel(to, PathType::VAULT_ROOT, PathType::FUSE_ROOT), userId,
                           shared_from_this()); err)
            throw std::runtime_error("Failed to rename: " + from.string() + " to " + to.string());
    }

    void Engine::copy(const fs::path &from, const fs::path &to, const unsigned int userId) {
        if (const auto err = Filesystem::copy(paths->absRelToAbsRel(from, PathType::VAULT_ROOT, PathType::FUSE_ROOT),
                         paths->absRelToAbsRel(to, PathType::VAULT_ROOT, PathType::FUSE_ROOT), userId,
                         shared_from_this()); err)
            throw std::runtime_error("Failed to copy: " + from.string() + " to " + to.string());
    }

    void Engine::remove(const fs::path &rel_path, const unsigned int userId) const {
        Filesystem::remove(paths->absRelToAbsRel(rel_path, PathType::VAULT_ROOT, PathType::FUSE_ROOT), userId);
    }

    void Engine::removeLocally(const fs::path &rel_path) const {
        const auto path = rel_path.string().front() != '/' ? fs::path("/" / rel_path) : rel_path;
        purgeThumbnails(path);
        auto file = db::query::fs::File::getFileByPath(vault->id, path);
        db::query::fs::File::deleteFile(vault->owner_id, file);

        if (const auto absPath = paths->absPath(path, PathType::BACKING_VAULT_ROOT); fs::exists(absPath))
            fs::remove(absPath);
    }

    void Engine::removeLocally(const std::shared_ptr<file::Trashed> &f) const {
        namespace fs = std::filesystem;

        fs::path absPath = paths->absPath(f->backing_path, PathType::BACKING_ROOT);

        // Remove the file if present
        std::error_code ec;
        fs::remove(absPath, ec); // ignore errors; file may not exist

        // Normalize roots to avoid string mismatch
        fs::path vaultRoot = paths->vaultRoot;
        vaultRoot = fs::weakly_canonical(vaultRoot, ec);
        absPath = fs::weakly_canonical(absPath, ec);

        // Walk up deleting now-empty dirs, but never above vaultRoot
        while (absPath.has_parent_path()) {
            fs::path parent = absPath.parent_path();

            // Stop if parent is (or is above) vaultRoot boundary
            // Use lexically_relative to detect containment robustly.

            if (const auto rel = parent.lexically_relative(vaultRoot);
                rel.empty() || rel.native().starts_with(".."))
                break; // outside or at boundary

            // If parent doesn't exist or isn't empty, we're done
            if (!fs::exists(parent) || !fs::is_empty(parent)) break;

            fs::remove(parent, ec);
            if (ec) break;

            absPath = parent;
        }

        const auto vaultPath = paths->absRelToAbsRel(f->path, PathType::FUSE_ROOT, PathType::VAULT_ROOT);

        for (const auto &size: Registry::get().caching.thumbnails.sizes) {
            const auto thumbPath = paths->absPath(vaultPath, PathType::THUMBNAIL_ROOT) / std::to_string(size);
            fs::remove(thumbPath, ec);
        }

        fs::remove(paths->absPath(vaultPath, PathType::CACHE_ROOT), ec);
    }
}