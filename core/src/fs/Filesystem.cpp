#include "fs/Filesystem.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "vault/model/Vault.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/Path.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/fs/File.hpp"
#include "config/Registry.hpp"
#include "db/query/fs/Entry.hpp"
#include "vault/EncryptionManager.hpp"
#include "fs/ops/file.hpp"
#include "crypto/util/hash.hpp"
#include "preview/thumbnail/Worker.hpp"
#include "fs/metadata/Magic.hpp"
#include "runtime/Deps.hpp"
#include "db/Transactions.hpp"
#include "log/Registry.hpp"
#include "db/query/vault/Vault.hpp"
#include "crypto/id/Generator.hpp"
#include "fs/cache/Registry.hpp"
#include "db/encoding/u8.hpp"

#include <ranges>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <exception>
#include <system_error>
#include <ctime>
#include <paths.h>

using namespace vh::storage;
using namespace vh::vault::model;
using namespace vh::db::encoding;
using namespace vh::config;
using namespace vh::crypto;
using namespace vh::fs;
using namespace vh::fs::ops;
using namespace vh::fs::model;
using namespace vh::fs::metadata;

static void updateFile(pqxx::work& txn, const std::shared_ptr<File>& file) {
    const auto exists = txn.exec(pqxx::prepped{"fs_entry_exists_by_inode"}, file->inode).one_field().as<bool>();
    const auto sizeRes = txn.exec(pqxx::prepped{"get_file_size_by_inode"}, file->inode);
    const auto existingSize = sizeRes.empty() ? 0 : sizeRes.one_field().as<unsigned int>();

    pqxx::params p;
    p.append(file->id);
    p.append(file->size_bytes);
    p.append(file->mime_type);
    p.append(file->content_hash);
    p.append(file->encryption_iv);

    txn.exec(pqxx::prepped{"update_file_only"}, p);

    std::optional<unsigned int> parentId = file->parent_id;
    while (parentId) {
        pqxx::params stats_params{parentId, file->size_bytes - existingSize, exists ? 0 : 1, 0}; // Increment size_bytes and file_count
        txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
        const auto res = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, parentId);
        if (res.empty()) break;
        parentId = res.one_field().as<std::optional<unsigned int>>();
    }
}

static void updateFSEntry(pqxx::work& txn, const std::shared_ptr<Entry>& entry) {
    pqxx::params p;
    p.append(entry->inode);
    p.append(entry->vault_id);
    p.append(entry->parent_id);
    p.append(entry->name);
    p.append(entry->base32_alias);
    p.append(entry->last_modified_by);
    p.append(to_utf8_string(entry->path.u8string()));
    p.append(entry->mode);
    p.append(entry->owner_uid);
    p.append(entry->group_gid);
    p.append(entry->is_hidden);
    p.append(entry->is_system);

    txn.exec(pqxx::prepped{"update_fs_entry_by_inode"}, p);
}

void Filesystem::init(const std::shared_ptr<Manager>& manager) {
    std::lock_guard lock(mutex_);
    storageManager_ = std::move(manager);
}

bool Filesystem::isReady() {
    std::lock_guard lock(mutex_);
    return static_cast<bool>(storageManager_);
}

int Filesystem::mkdir(const std::filesystem::path& absPath,
                      const mode_t mode,
                      const std::optional<unsigned int>& userId,
                      std::shared_ptr<Engine> engine) {
    log::Registry::fs()->debug("Creating directory at: {}", absPath.string());

    std::scoped_lock lock(mutex_);

    try {
        if (!storageManager_) {
            log::Registry::fs()->error("[Filesystem::mkdir] StorageManager is not initialized");
            return -EIO;
        }

        const auto& cache = runtime::Deps::get().fsCache;

        if (absPath.empty()) {
            log::Registry::fs()->error("[Filesystem::mkdir] Cannot create directory at empty path");
            return -EINVAL;
        }

        std::vector<std::filesystem::path> toCreate;
        std::filesystem::path cur = absPath;

        while (!cur.empty() && !cache->entryExists(cur)) {
            toCreate.push_back(cur);
            cur = cur.parent_path();
        }

        std::ranges::reverse(toCreate);

        if (!engine)
            engine = storageManager_->resolveStorageEngine(absPath);

        if (!engine) {
            log::Registry::fs()->error("[Filesystem::mkdir] No storage engine found for path: {}", absPath.string());
            return -EIO;
        }

        for (const auto& p : toCreate) {
            const auto path = makeAbsolute(p);
            log::Registry::fs()->debug("[Filesystem::mkdir] Creating directory at: {}", path.string());

            auto dir = std::make_shared<Directory>();

            dir->vault_id = engine->vault->id;
            dir->path = engine->paths->absRelToAbsRel(path, PathType::FUSE_ROOT, PathType::VAULT_ROOT);

            const auto parent = cache->getEntry(resolveParent(path));
            if (!parent) {
                log::Registry::fs()->error(
                    "[Filesystem::mkdir] Parent directory does not exist: {}",
                    path.parent_path().string());
                return -ENOENT;
            }

            dir->parent_id = parent->id;
            dir->fuse_path = path;
            dir->name = path.filename();
            dir->base32_alias = id::Generator({ .namespace_token = dir->name }).generate();
            dir->backing_path = parent->backing_path / dir->base32_alias;
            dir->mode = mode;
            dir->owner_uid = getuid();
            dir->group_gid = getgid();
            dir->inode = cache->assignInode(path);
            dir->is_hidden = !dir->name.empty() && dir->name.front() == '.' && !dir->name.starts_with("..");
            dir->is_system = false;
            dir->created_by = dir->last_modified_by = userId;

            dir->id = db::query::fs::Directory::upsertDirectory(dir);
            cache->cacheEntry(dir);

            try {
                std::filesystem::create_directories(dir->backing_path);
            } catch (const std::filesystem::filesystem_error& e) {
                log::Registry::fs()->error(
                    "[Filesystem::mkdir] Failed to create backing directory {}: {}",
                    dir->backing_path.string(),
                    e.what());

                if (e.code())
                    return -e.code().value();

                return -EIO;
            }
        }

        log::Registry::fs()->debug("Successfully created directory at: {}", absPath.string());
        return 0;
    } catch (const std::bad_alloc& ex) {
        log::Registry::fs()->error("[Filesystem::mkdir] Out of memory for {}: {}", absPath.string(), ex.what());
        return -ENOMEM;
    } catch (const std::filesystem::filesystem_error& ex) {
        log::Registry::fs()->error("[Filesystem::mkdir] Filesystem error for {}: {}", absPath.string(), ex.what());
        return ex.code() ? -ex.code().value() : -EIO;
    } catch (const std::exception& ex) {
        log::Registry::fs()->error("[Filesystem::mkdir] Failed to create directory {}: {}", absPath.string(), ex.what());
        return -EIO;
    } catch (...) {
        log::Registry::fs()->error("[Filesystem::mkdir] Unknown failure creating directory: {}", absPath.string());
        return -EIO;
    }
}

void Filesystem::mkVault(const std::filesystem::path& absPath, unsigned int vaultId, mode_t mode) {
    if (absPath.empty()) throw std::runtime_error("Cannot create directory at empty path");
    log::Registry::fs()->debug("Creating vault directory at: {}", absPath.string());

    std::scoped_lock lock(mutex_);
    if (!storageManager_) throw std::runtime_error("StorageManager is not initialized");

    const auto vault = db::query::vault::Vault::getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault with ID " + std::to_string(vaultId) + " does not exist");

    try {
        const auto dir = std::make_shared<Directory>();
        dir->vault_id = vaultId;
        dir->path = "/";
        dir->name = to_snake_case(vault->name);
        dir->parent_id = db::query::fs::Entry::getRootEntry()->id;
        dir->base32_alias = vault->mount_point;
        dir->backing_path = paths::getBackingPath() / dir->base32_alias;
        dir->fuse_path = absPath;
        dir->created_at = dir->updated_at = std::time(nullptr);
        dir->mode = mode;
        dir->owner_uid = getuid();
        dir->group_gid = getgid();
        dir->inode = runtime::Deps::get().fsCache->assignInode(absPath);
        dir->is_hidden = false;
        dir->is_system = false;

        const auto root = db::query::fs::Entry::getRootEntry();

        dir->id = db::query::fs::Directory::upsertDirectory(dir);

        runtime::Deps::get().fsCache->cacheEntry(dir);

        if (!std::filesystem::exists(dir->backing_path)) std::filesystem::create_directories(dir->backing_path);

        log::Registry::fs()->debug("Successfully created vault directory at: {}", absPath.string());
    } catch (const std::exception& ex) {
        log::Registry::fs()->error("Failed to create vault directory: {} - {}", absPath.string(), ex.what());
        throw std::runtime_error("Failed to create vault directory: " + absPath.string() + " - " + ex.what());
    }
}

bool Filesystem::exists(const std::filesystem::path& absPath) {
    try {
        return runtime::Deps::get().fsCache->entryExists(absPath);
    } catch (const std::exception& ex) {
        log::Registry::fs()->error("Error checking existence of path {}: {}", absPath.string(), ex.what());
        return false;
    }
}

int Filesystem::copy(const std::filesystem::path& from,
                     const std::filesystem::path& to,
                     const unsigned int userId,
                     std::shared_ptr<Engine> engine) {
    std::scoped_lock lock(mutex_);

    try {
        if (!storageManager_) {
            log::Registry::fs()->error("[Filesystem::copy] StorageManager is not initialized");
            return -EIO;
        }

        if (from == to)
            return 0;

        if (!engine)
            engine = storageManager_->resolveStorageEngine(from);

        if (!engine) {
            log::Registry::fs()->error(
                "[Filesystem::copy] No storage engine found for source path: {}",
                from.string());
            return -EIO;
        }

        const auto toEngine = storageManager_->resolveStorageEngine(to);
        if (!toEngine) {
            log::Registry::fs()->error(
                "[Filesystem::copy] No storage engine found for destination path: {}",
                to.string());
            return -EIO;
        }

        if (toEngine->vault->id != engine->vault->id) {
            log::Registry::fs()->error(
                "[Filesystem::copy] Cross-vault copy not supported: {} -> {}",
                from.string(),
                to.string());
            return -EXDEV;
        }

        const auto fromVaultPath = engine->paths->absRelToAbsRel(from, PathType::FUSE_ROOT, PathType::VAULT_ROOT);
        const auto toVaultPath = engine->paths->absRelToAbsRel(to, PathType::FUSE_ROOT, PathType::VAULT_ROOT);

        const bool isFile = engine->isFile(fromVaultPath);
        if (!isFile && !engine->isDirectory(fromVaultPath)) {
            log::Registry::fs()->error(
                "[Filesystem::copy] Source path does not exist: {}",
                from.string());
            return -ENOENT;
        }

        const auto& cache = runtime::Deps::get().fsCache;

        const auto entry = cache->getEntry(from);
        if (!entry) {
            log::Registry::fs()->error(
                "[Filesystem::copy] Source entry not found in cache: {}",
                from.string());
            return -ENOENT;
        }

        const auto parent = cache->getEntry(resolveParent(to));
        if (!parent) {
            log::Registry::fs()->error(
                "[Filesystem::copy] Destination parent does not exist: {}",
                to.parent_path().string());
            return -ENOENT;
        }

        if (cache->entryExists(to)) {
            log::Registry::fs()->error(
                "[Filesystem::copy] Destination already exists: {}",
                to.string());
            return -EEXIST;
        }

        if (isFile) {
            auto copied = std::make_shared<File>(*std::static_pointer_cast<File>(entry));
            copied->id = 0;
            copied->path = toVaultPath;
            copied->fuse_path = to;
            copied->name = to.filename().string();
            copied->base32_alias = id::Generator({ .namespace_token = copied->name }).generate();
            copied->backing_path = parent->backing_path / copied->base32_alias;
            copied->created_by = copied->last_modified_by = userId;
            copied->parent_id = parent->id;
            copied->inode = cache->getOrAssignInode(to);
            copied->is_hidden = !copied->name.empty() && copied->name.front() == '.' && !copied->name.starts_with("..");
            copied->is_system = false;

            copied->id = db::query::fs::File::upsertFile(copied);
            cache->cacheEntry(copied);
        } else {
            auto copied = std::make_shared<Directory>(*std::static_pointer_cast<Directory>(entry));
            copied->id = 0;
            copied->path = toVaultPath;
            copied->fuse_path = to;
            copied->name = to.filename().string();
            copied->base32_alias = id::Generator({ .namespace_token = copied->name }).generate();
            copied->backing_path = parent->backing_path / copied->base32_alias;
            copied->created_by = copied->last_modified_by = userId;
            copied->parent_id = parent->id;
            copied->inode = cache->getOrAssignInode(to);
            copied->is_hidden = !copied->name.empty() && copied->name.front() == '.' && !copied->name.starts_with("..");
            copied->is_system = false;

            copied->id = db::query::fs::Directory::upsertDirectory(copied);
            cache->cacheEntry(copied);
        }

        log::Registry::fs()->debug("[Filesystem::copy] Successfully copied {} -> {}", from.string(), to.string());
        return 0;
    } catch (const std::bad_alloc& ex) {
        log::Registry::fs()->error("[Filesystem::copy] Out of memory copying {} -> {}: {}", from.string(), to.string(), ex.what());
        return -ENOMEM;
    } catch (const std::filesystem::filesystem_error& ex) {
        log::Registry::fs()->error("[Filesystem::copy] Filesystem error copying {} -> {}: {}", from.string(), to.string(), ex.what());
        return ex.code() ? -ex.code().value() : -EIO;
    } catch (const std::exception& ex) {
        log::Registry::fs()->error("[Filesystem::copy] Failed to copy {} -> {}: {}", from.string(), to.string(), ex.what());
        return -EIO;
    } catch (...) {
        log::Registry::fs()->error("[Filesystem::copy] Unknown failure copying {} -> {}", from.string(), to.string());
        return -EIO;
    }
}

void Filesystem::remove(const std::filesystem::path& path, const unsigned int userId) {
    const auto& cache = runtime::Deps::get().fsCache;
    const auto entry = cache->getEntry(path);
    if (!entry) throw std::runtime_error("[Filesystem] Path does not exist in cache: " + path.string());
    if (!entry->vault_id) throw std::runtime_error("[Filesystem] Entry has no associated vault ID: " + path.string());

    // !!! DO NOT CALL THIS FUNCTION DIRECTLY FROM FUSE CALLBACKS - WEBSOCKET OR INTERNAL ONLY !!!
    // This function recursively marks files as trashed in the database and deletes their backing paths
    // which is incompatible with how FUSE expects unlink/rmdir to behave.

    if (entry->isDirectory())
        for (const auto& file : db::query::fs::File::listFilesInDir(*entry->vault_id, entry->path, true)) {
            db::query::fs::File::markFileAsTrashed(userId, file->id);
            cache->evictPath(file->fuse_path);
        }
    else {
        db::query::fs::File::markFileAsTrashed(userId, *entry->vault_id, entry->path);
        cache->evictPath(path);
    }

    if (std::filesystem::exists(entry->backing_path)) std::filesystem::remove_all(entry->backing_path);
}

std::shared_ptr<File> Filesystem::createFile(const NewFileContext& ctx) {
    const auto engine = ctx.engine ? ctx.engine : storageManager_->resolveStorageEngine(ctx.path);
    if (!engine) throw std::runtime_error("[Filesystem] No storage engine found for file creation");

    log::Registry::fs()->debug("Creating file at path: {}, fuse_path: {}", ctx.path.string(), ctx.fuse_path.string());

    const auto& cache = runtime::Deps::get().fsCache;

    if (ctx.path.empty()) throw std::runtime_error("Cannot create file at empty path");
    if (cache->entryExists(ctx.fuse_path)) {
        if (!ctx.overwrite) {
            log::Registry::fs()->warn("File already exists at path: {}", ctx.fuse_path.string());
            const auto entry = cache->getEntry(ctx.fuse_path);
            if (entry->isDirectory()) throw std::filesystem::filesystem_error(
                "[Filesystem] Cannot create file at path, a directory already exists",
                ctx.fuse_path,
                std::make_error_code(std::errc::file_exists));
            return std::static_pointer_cast<File>(entry);
        }

        log::Registry::fs()->debug("File already exists at path: {}, overwriting", ctx.fuse_path.string());
        auto entry = cache->getEntry(ctx.fuse_path);
        if (entry->isDirectory()) {
            log::Registry::fs()->error("Cannot overwrite directory with file at path: {}", ctx.fuse_path.string());
            throw std::filesystem::filesystem_error(
                "[Filesystem] Cannot overwrite directory with file",
                ctx.fuse_path,
                std::make_error_code(std::errc::file_exists));
        }

        const auto f = std::static_pointer_cast<File>(entry);

        f->content_hash = hash::blake2b(entry->backing_path);

        if (!ctx.buffer.empty()) {
            const auto ciphertext = engine->encryptionManager->encrypt(ctx.buffer, f);
            writeFile(entry->backing_path, ciphertext);
            f->size_bytes = ctx.buffer.size();
            f->mime_type = Magic::get_mime_type_from_buffer(ctx.buffer);
        } else {
            f->size_bytes = std::filesystem::file_size(entry->backing_path);
            f->mime_type = inferMimeTypeFromPath(ctx.path);
        }

        db::query::fs::File::updateFile(f);

        return f;
    }

    const auto parent = runtime::Deps::get().fsCache->getEntry(resolveParent(ctx.fuse_path));
    if (!parent) {
        log::Registry::fs()->error("Parent directory does not exist for path: {}", ctx.fuse_path.string());
        throw std::filesystem::filesystem_error(
            "[Filesystem] Parent directory does not exist",
            ctx.fuse_path,
            std::make_error_code(std::errc::no_such_file_or_directory));
    }

    const auto f = std::make_shared<File>();
    f->parent_id = parent->id;
    f->vault_id = engine->vault->id;
    f->name = ctx.path.filename();
    f->path = ctx.path;
    f->fuse_path = ctx.fuse_path;
    f->base32_alias = id::Generator({ .namespace_token = f->name }).generate();
    f->backing_path = parent->backing_path / f->base32_alias;
    f->mode = ctx.mode;
    f->owner_uid = ctx.linux_uid;
    f->group_gid = ctx.linux_gid;
    f->is_hidden = ctx.path.filename().string().starts_with('.');
    f->created_by = f->last_modified_by = ctx.userId;
    f->inode = std::make_optional(cache->getOrAssignInode(ctx.fuse_path));
    f->mime_type = ctx.buffer.empty() ? inferMimeTypeFromPath(ctx.path) : Magic::get_mime_type_from_buffer(ctx.buffer);
    f->size_bytes = ctx.buffer.size();

    if (ctx.buffer.empty()) std::ofstream(f->backing_path).close();
    else {
        std::string iv_b64;
        const auto ciphertext = engine->encryptionManager->encrypt(ctx.buffer, f);
        writeFile(f->backing_path, ciphertext);
    }

    f->content_hash = hash::blake2b(f->backing_path);

    if (!std::filesystem::exists(f->backing_path))
        throw std::runtime_error("[Filesystem] Failed to create real file at: " + f->backing_path.string());

    f->id = db::query::fs::File::upsertFile(f);
    cache->cacheEntry(f);

    if (f->size_bytes > 0 && f->mime_type && isPreviewable(*f->mime_type))
        preview::thumbnail::Worker::enqueue(engine, ctx.buffer, f);

    log::Registry::fs()->debug("Successfully created file at path: {}", ctx.path.string());
    return f;
}

std::pair<int, std::shared_ptr<model::Entry>>
Filesystem::createFile(const std::filesystem::path& path,
                       const uid_t uid,
                       const gid_t gid,
                       const mode_t mode) {
    std::scoped_lock lock(mutex_);

    try {
        if (!storageManager_) {
            log::Registry::fs()->error("[Filesystem::createFile] StorageManager is not initialized");
            return {-EIO, nullptr};
        }

        const auto engine = storageManager_->resolveStorageEngine(path);
        if (!engine) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] No storage engine found for path: {}",
                path.string());
            return {-EIO, nullptr};
        }

        const auto& cache = runtime::Deps::get().fsCache;

        log::Registry::fs()->debug("[Filesystem::createFile] Creating file at path: {}", path.string());

        const auto parent = cache->getEntry(resolveParent(path));
        if (!parent) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] Parent directory does not exist: {}",
                path.parent_path().string());
            return {-ENOENT, nullptr};
        }

        if (cache->entryExists(path)) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] File already exists: {}",
                path.string());
            return {-EEXIST, nullptr};
        }

        auto f = std::make_shared<File>();
        f->parent_id = parent->id;
        f->vault_id = engine->vault->id;
        f->name = path.filename();
        f->path = engine->paths->absRelToAbsRel(path, PathType::FUSE_ROOT, PathType::VAULT_ROOT);
        f->fuse_path = path;
        f->base32_alias = id::Generator({ .namespace_token = f->name }).generate();
        f->backing_path = parent->backing_path / f->base32_alias;
        f->mode = mode;
        f->owner_uid = uid;
        f->group_gid = gid;
        f->is_hidden = !f->name.empty() && f->name.starts_with('.');
        f->created_at = std::time(nullptr);
        f->updated_at = f->created_at;
        f->inode = std::make_optional(cache->getOrAssignInode(path));
        f->mime_type = inferMimeTypeFromPath(path.filename());
        f->size_bytes = 0;

        try {
            std::filesystem::create_directories(f->backing_path.parent_path());
            std::ofstream(f->backing_path).close();
        } catch (const std::filesystem::filesystem_error& ex) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] Failed to create backing file for {}: {}",
                path.string(),
                ex.what());
            return {ex.code() ? -ex.code().value() : -EIO, nullptr};
        } catch (const std::exception& ex) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] Failed to create backing file for {}: {}",
                path.string(),
                ex.what());
            return {-EIO, nullptr};
        }

        if (!std::filesystem::exists(f->backing_path)) {
            log::Registry::fs()->error(
                "[Filesystem::createFile] Failed to create real file at: {}",
                f->backing_path.string());
            return {-EIO, nullptr};
        }

        f->content_hash = hash::blake2b(f->backing_path);
        f->id = db::query::fs::File::upsertFile(f);
        cache->cacheEntry(f);

        log::Registry::fs()->debug("[Filesystem::createFile] Successfully created file at path: {}", path.string());
        return {0, f};
    } catch (const std::bad_alloc& ex) {
        log::Registry::fs()->error(
            "[Filesystem::createFile] Out of memory creating {}: {}",
            path.string(),
            ex.what());
        return {-ENOMEM, nullptr};
    } catch (const std::filesystem::filesystem_error& ex) {
        log::Registry::fs()->error(
            "[Filesystem::createFile] Filesystem error creating {}: {}",
            path.string(),
            ex.what());
        return {ex.code() ? -ex.code().value() : -EIO, nullptr};
    } catch (const std::exception& ex) {
        log::Registry::fs()->error(
            "[Filesystem::createFile] Failed to create file at path {}: {}",
            path.string(),
            ex.what());
        return {-EIO, nullptr};
    } catch (...) {
        log::Registry::fs()->error(
            "[Filesystem::createFile] Unknown failure creating file at path: {}",
            path.string());
        return {-EIO, nullptr};
    }
}

int Filesystem::rename(const std::filesystem::path& oldPath,
                       const std::filesystem::path& newPath,
                       const std::optional<unsigned int>& userId,
                       std::shared_ptr<Engine> engine) {
    std::scoped_lock lock(mutex_);

    try {
        log::Registry::fs()->debug("[Filesystem::rename] Renaming {} to {}", oldPath.string(), newPath.string());

        if (!storageManager_) {
            log::Registry::fs()->error("[Filesystem::rename] StorageManager is not initialized");
            return -EIO;
        }

        if (oldPath == newPath)
            return 0;

        const auto entry = runtime::Deps::get().fsCache->getEntry(oldPath);
        if (!entry) {
            log::Registry::fs()->error(
                "[Filesystem::rename] Source entry not found: {}",
                oldPath.string());
            return -ENOENT;
        }

        if (!engine)
            engine = storageManager_->resolveStorageEngine(oldPath);

        if (!engine) {
            log::Registry::fs()->error(
                "[Filesystem::rename] No storage engine found for source path: {}",
                oldPath.string());
            return -ENOENT;
        }

        const auto toEngine = storageManager_->resolveStorageEngine(newPath);
        if (!toEngine) {
            log::Registry::fs()->error(
                "[Filesystem::rename] No storage engine found for destination path: {}",
                newPath.string());
            return -ENOENT;
        }

        if (toEngine->vault->id != engine->vault->id) {
            log::Registry::fs()->error(
                "[Filesystem::rename] Cross-vault rename not supported: {} -> {}",
                oldPath.string(),
                newPath.string());
            return -EXDEV;
        }

        const auto oldBackingPath = entry->backing_path;

        int rc = 0;

        db::Transactions::exec("Filesystem::rename", [&](pqxx::work& txn) {
            std::vector<uint8_t> buffer;

            if (entry->isDirectory()) {
                if (!entry->parent_id) {
                    log::Registry::fs()->error(
                        "[Filesystem::rename] Cannot rename root directory: {}",
                        oldPath.string());
                    rc = -EBUSY;
                    return;
                }

                for (const auto& item : runtime::Deps::get().fsCache->listDir(*entry->parent_id, true)) {
                    rc = handleRename({
                        .from = item->fuse_path,
                        .to = updateSubdirPath(oldPath, newPath, item->fuse_path),
                        .buffer = buffer,
                        .userId = userId,
                        .engine = engine,
                        .entry = item,
                        .txn = txn
                    });

                    if (rc != 0)
                        return;

                    buffer.clear();
                }
            }

            rc = handleRename({
                .from = oldPath,
                .to = newPath,
                .buffer = buffer,
                .userId = userId,
                .engine = engine,
                .entry = entry,
                .txn = txn
            });

            if (rc != 0)
                return;

            txn.commit();

            try {
                if (entry->backing_path != oldBackingPath)
                    std::filesystem::remove_all(oldBackingPath);
            } catch (const std::filesystem::filesystem_error& ex) {
                log::Registry::fs()->warn(
                    "[Filesystem::rename] Renamed successfully but failed to remove old backing path {}: {}",
                    oldBackingPath.string(),
                    ex.what());
            }
        });

        if (rc != 0)
            return rc;

        runtime::Deps::get().fsCache->evictPath(oldPath);
        runtime::Deps::get().fsCache->evictPath(newPath);
        runtime::Deps::get().fsCache->cacheEntry(entry);

        log::Registry::fs()->debug("[Filesystem::rename] Successfully renamed {} to {}", oldPath.string(), newPath.string());
        return 0;
    } catch (const std::bad_alloc& ex) {
        log::Registry::fs()->error(
            "[Filesystem::rename] Out of memory renaming {} -> {}: {}",
            oldPath.string(),
            newPath.string(),
            ex.what());
        return -ENOMEM;
    } catch (const std::filesystem::filesystem_error& ex) {
        log::Registry::fs()->error(
            "[Filesystem::rename] Filesystem error renaming {} -> {}: {}",
            oldPath.string(),
            newPath.string(),
            ex.what());
        return ex.code() ? -ex.code().value() : -EIO;
    } catch (const std::exception& ex) {
        log::Registry::fs()->error(
            "[Filesystem::rename] Failed to rename {} -> {}: {}",
            oldPath.string(),
            newPath.string(),
            ex.what());
        return -EIO;
    } catch (...) {
        log::Registry::fs()->error(
            "[Filesystem::rename] Unknown failure renaming {} -> {}",
            oldPath.string(),
            newPath.string());
        return -EIO;
    }
}

int Filesystem::handleRename(const RenameContext& ctx) {
    try {
        const auto& cache = runtime::Deps::get().fsCache;
        const auto& entry = ctx.entry;

        if (!entry) {
            log::Registry::fs()->error("[Filesystem::handleRename] Null entry for {} -> {}", ctx.from.string(), ctx.to.string());
            return -EINVAL;
        }

        const auto oldBackingPath = entry->backing_path;
        if (!std::filesystem::exists(oldBackingPath)) {
            log::Registry::fs()->error(
                "[Filesystem::handleRename] Source backing path does not exist: {}",
                oldBackingPath.string());
            return -ENOENT;
        }

        unsigned int id = 0;
        if (entry->id == 0) {
            const auto e = cache->getEntry(entry->fuse_path);
            id = e ? e->id : 0;
        } else {
            id = entry->id;
        }

        const auto parent = cache->getEntry(resolveParent(ctx.to));
        if (!parent) {
            log::Registry::fs()->error(
                "[Filesystem::handleRename] Parent directory does not exist: {}",
                resolveParent(ctx.to).string());
            return -ENOENT;
        }

        const auto oldVaultPath = entry->path;

        entry->id = id;
        entry->name = ctx.to.filename();
        entry->path = ctx.engine->paths->absRelToAbsRel(ctx.to, PathType::FUSE_ROOT, PathType::VAULT_ROOT);
        entry->fuse_path = ctx.to;
        entry->parent_id = parent->id;
        entry->created_by = entry->last_modified_by = ctx.userId;
        entry->backing_path = parent->backing_path / entry->base32_alias;

        if (entry->isDirectory()) {
            std::filesystem::create_directories(entry->backing_path);
        } else {
            std::filesystem::create_directories(entry->backing_path.parent_path());

            const auto f = std::static_pointer_cast<File>(entry);

            if (canFastPath(entry, ctx.engine)) {
                log::Registry::fs()->debug("[Filesystem::handleRename] Fast path rename for file: {}", ctx.from.string());

                std::filesystem::rename(oldBackingPath, entry->backing_path);
                updateFSEntry(ctx.txn, entry);

                cache->evictPath(ctx.from);
                cache->evictPath(ctx.to);
                cache->cacheEntry(entry);
                return 0;
            }

            auto buffer = ctx.buffer;

            if (!f->encryption_iv.empty()) {
                const auto tmp = decrypt_file_to_temp(ctx.engine->vault->id, oldVaultPath, ctx.engine);
                buffer = readFileToVector(tmp);
            } else {
                buffer = readFileToVector(oldBackingPath);
            }

            if (buffer.empty()) {
                std::ofstream(entry->backing_path).close();

                if (!std::filesystem::exists(entry->backing_path)) {
                    log::Registry::fs()->error(
                        "[Filesystem::handleRename] Failed to create real file at: {}",
                        entry->backing_path.string());
                    return -EIO;
                }
            } else {
                const auto ciphertext = ctx.engine->encryptionManager->encrypt(buffer, f);
                if (ciphertext.empty()) {
                    log::Registry::fs()->error(
                        "[Filesystem::handleRename] Encryption failed for file: {}",
                        oldBackingPath.string());
                    return -EIO;
                }

                writeFile(entry->backing_path, ciphertext);

                f->size_bytes = std::filesystem::file_size(entry->backing_path);
                f->mime_type = Magic::get_mime_type_from_buffer(buffer);
                f->content_hash = hash::blake2b(entry->backing_path);

                if (f->size_bytes > 0 && f->mime_type && isPreviewable(*f->mime_type))
                    preview::thumbnail::Worker::enqueue(ctx.engine, buffer, f);

                updateFile(ctx.txn, f);
            }
        }

        updateFSEntry(ctx.txn, entry);

        cache->evictPath(ctx.from);
        cache->evictPath(ctx.to);
        cache->cacheEntry(entry);

        log::Registry::fs()->debug("[Filesystem::handleRename] Renamed {} to {}", ctx.from.string(), ctx.to.string());
        return 0;
    } catch (const std::bad_alloc& ex) {
        log::Registry::fs()->error(
            "[Filesystem::handleRename] Out of memory renaming {} -> {}: {}",
            ctx.from.string(),
            ctx.to.string(),
            ex.what());
        return -ENOMEM;
    } catch (const std::filesystem::filesystem_error& ex) {
        log::Registry::fs()->error(
            "[Filesystem::handleRename] Filesystem error renaming {} -> {}: {}",
            ctx.from.string(),
            ctx.to.string(),
            ex.what());
        return ex.code() ? -ex.code().value() : -EIO;
    } catch (const std::exception& ex) {
        log::Registry::fs()->error(
            "[Filesystem::handleRename] Failed to rename {} -> {}: {}",
            ctx.from.string(),
            ctx.to.string(),
            ex.what());
        return -EIO;
    } catch (...) {
        log::Registry::fs()->error(
            "[Filesystem::handleRename] Unknown failure renaming {} -> {}",
            ctx.from.string(),
            ctx.to.string());
        return -EIO;
    }
}

bool Filesystem::canFastPath(const std::shared_ptr<Entry>& entry, const std::shared_ptr<Engine>& engine) {
    if (entry->isDirectory()) return false;
    const auto file = std::static_pointer_cast<File>(entry);
    if (file->encryption_iv.empty()) return false;
    return file->vault_id == engine->vault->id;
}

bool Filesystem::isPreviewable(const std::string& mimeType) {
    return mimeType.starts_with("image") || mimeType.starts_with("application") || mimeType.contains("pdf");
}
