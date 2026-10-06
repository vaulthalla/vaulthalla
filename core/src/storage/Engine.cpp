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
#include "identities/User.hpp"
#include "storage/GcmFileReader.hpp"
#include "fs/cache/Registry.hpp"
#include "runtime/Deps.hpp"
#include "crypto/util/encrypt.hpp"

#include <system_error>

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
          paths(std::make_shared<Path>(makeAbsolute(vault->effectiveFuseName()), vault->mount_point)),
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

    namespace {
        class EngineEmptyReader final : public PlaintextReader {
        public:
            explicit EngineEmptyReader(Generation g) : generation_(std::move(g)) {}
            [[nodiscard]] uint64_t size() const override { return 0; }
            std::size_t read(uint64_t, std::span<uint8_t>) override { return 0; }
            [[nodiscard]] const Generation &generation() const override { return generation_; }
        private:
            Generation generation_;
        };
    }

    std::unique_ptr<PlaintextReader> Engine::openPlaintextReader(const std::shared_ptr<File> &f,
                                                                 const ReaderOptions options) const {
        if (!f) throw std::invalid_argument("Cannot read a null file");
        auto generation = generationOf(*f);
        if (vault) generation.vault_id = vault->id;
        if (f->size_bytes == 0) return std::make_unique<EngineEmptyReader>(std::move(generation));

        std::error_code ec;
        if (!fs::exists(f->backing_path, ec)) return openMissingReader(f, options);
        return openLocalReader(f, options);
    }

    std::unique_ptr<PlaintextReader> Engine::openMissingReader(const std::shared_ptr<File> &f,
                                                               const ReaderOptions &) const {
        throw std::runtime_error("File content not found: " + f->path.string());
    }

    std::unique_ptr<PlaintextReader> Engine::openLocalReader(const std::shared_ptr<File> &f,
                                                             const ReaderOptions &options) const {
        GcmFileReader::Params params;
        params.path = f->backing_path;
        params.plaintextSize = f->size_bytes;
        params.generation = generationOf(*f);
        if (vault) params.generation.vault_id = vault->id;
        params.integrityDomain = "file:" + std::to_string(params.generation.vault_id) + ":" + std::to_string(f->id);
        params.strict = resolveIntegrityPolicy(options.integrity) == IntegrityPolicy::Strict;

        if (!f->encryption_iv.empty()) {
            if (!encryptionManager) throw std::runtime_error("Vault has no encryption key loaded");
            const auto iv = crypto::util::b64_decode(f->encryption_iv);
            if (iv.size() != params.iv.size()) throw std::runtime_error("Invalid stored IV for " + f->path.string());
            std::ranges::copy(iv, params.iv.begin());
            params.key = encryptionManager->keySnapshot(f->encrypted_with_key_version);

            // On a tag mismatch: was the file resealed (new IV) while we read the old one? Then it's a race.
            params.stillCurrent = [fusePath = f->fuse_path, iv = f->encryption_iv]() {
                const auto& cache = runtime::Deps::get().fsCache;
                if (!cache || fusePath.empty()) return true;
                const auto entry = std::dynamic_pointer_cast<File>(cache->getEntry(fusePath));
                return entry && entry->encryption_iv == iv;
            };
        }
        return std::make_unique<GcmFileReader>(std::move(params));
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
        // A vault that has not stored anything yet (a new or S3 vault) has no directory: that is 0 bytes, not an
        // error. Files that vanish mid-walk (uploads, eviction) are skipped rather than failing the whole sum.
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) return 0;

        uintmax_t total = 0;
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        if (ec) return 0;
        for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || entryEc) continue;
            const auto size = it->file_size(entryEc);
            if (!entryEc) total += size;
        }
        return total;
    }

    // This vault's own backing tree (backingPath/<mount_point>), not the shared backing root that holds every vault
    // and the cache (#161: every vault used to report, and be quota-checked against, the sum of all of them).
    uintmax_t Engine::getVaultSize() const { return getDirectorySize(paths->backingVaultRoot); }
    // Derived preview artifacts (<cacheRoot>/derived) are disposable, non-authoritative and bounded globally by
    // caching.max_size_mb, so they are not charged to the vault's quota.
    uintmax_t Engine::getCacheSize() const {
        const auto total = getDirectorySize(paths->cacheRoot);
        const auto derived = getDirectorySize(paths->cacheRoot / "derived");
        return total > derived ? total - derived : 0;
    }
    uintmax_t Engine::getVaultAndCacheTotalSize() const { return getVaultSize() + getCacheSize(); }
    uintmax_t Engine::freeSpace() const {
        if (!vault) return 0;

        if (vault->quota == 0) {
            if (!paths) return 0;

            std::error_code ec;
            const auto info = fs::space(paths->backingRoot, ec);
            if (ec || info.available <= MIN_FREE_SPACE) return 0;
            return info.available - MIN_FREE_SPACE;
        }

        const auto usedWithReserve = getVaultAndCacheTotalSize() + MIN_FREE_SPACE;
        return vault->quota > usedWithReserve ? vault->quota - usedWithReserve : 0;
    }

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

            if (const auto err = Filesystem::mkdir({.path = toPath.parent_path()}); err)
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

            if (const auto err = Filesystem::mkdir({.path = toPath.parent_path()}); err)
                throw std::runtime_error("Failed to create thumbnail directory: " + toPath.parent_path().string() + " Error: " + std::to_string(err));

            fs::copy_file(fromPath, toPath, fs::copy_options::overwrite_existing);
        }
    }

    void Engine::mkdir(const fs::path &relPath, const std::shared_ptr<identities::User>& user) {
        if (const auto err = Filesystem::mkdir({
                .path = vaultPathToFusePath(relPath),
                .engine = shared_from_this(),
                .user = user
            }); err)
            throw std::runtime_error("Failed to create directory: " + relPath.string());
    }

    void Engine::move(const fs::path &from, const fs::path &to, const std::shared_ptr<identities::User>& user) {
        if (const auto err = Filesystem::rename(vaultPathToFusePath(from),
                           vaultPathToFusePath(to), user,
                           shared_from_this()); err)
            throw std::runtime_error("Failed to move directory: " + from.string() + " to " + to.string());
    }

    void Engine::rename(const fs::path &from, const fs::path &to, const std::shared_ptr<identities::User>& user) {
        if (const auto err = Filesystem::rename(vaultPathToFusePath(from),
                           vaultPathToFusePath(to), user,
                           shared_from_this()); err)
            throw std::runtime_error("Failed to rename: " + from.string() + " to " + to.string());
    }

    void Engine::copy(const fs::path &from, const fs::path &to, const unsigned int userId) {
        if (const auto err = Filesystem::copy(vaultPathToFusePath(from),
                         vaultPathToFusePath(to), userId,
                         shared_from_this()); err)
            throw std::runtime_error("Failed to copy: " + from.string() + " to " + to.string());
    }

    void Engine::remove(const fs::path &rel_path, const unsigned int userId) const {
        Filesystem::remove(vaultPathToFusePath(rel_path), userId);
    }

    void Engine::removeLocally(const fs::path &rel_path) const {
        const auto path = rel_path.string().front() != '/' ? fs::path("/" / rel_path) : rel_path;
        purgeThumbnails(path);
        const auto file = db::query::fs::File::getFileByPath(vault->id, path);
        db::query::fs::File::deleteFile(vault->owner_id, file);

        if (const auto absPath = paths->absPath(path, PathType::BACKING_VAULT_ROOT); fs::exists(absPath))
            fs::remove(absPath);
    }

    void Engine::removeLocally(const std::shared_ptr<file::Trashed> &f) const {
        namespace fs = std::filesystem;

        fs::path absPath = f->backing_path;
        if (absPath.is_relative()) absPath = paths->absPath(absPath, PathType::BACKING_ROOT);

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

        const auto vaultPath = makeAbsolute(f->path);

        for (const auto &size: Registry::get().caching.thumbnails.sizes) {
            const auto thumbPath = paths->absPath(vaultPath, PathType::THUMBNAIL_ROOT) / std::to_string(size);
            fs::remove(thumbPath, ec);
        }

        fs::remove(paths->absPath(vaultPath, PathType::CACHE_ROOT), ec);
    }

    std::filesystem::path Engine::vaultPathToFusePath(const std::filesystem::path &vPath) const {
        return paths->absRelToAbsRel(vPath, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    }

    std::filesystem::path Engine::fusePathToVaultPath(const std::filesystem::path &fPath) const {
        return paths->absRelToAbsRel(fPath, PathType::FUSE_ROOT, PathType::VAULT_ROOT);
    }
}
