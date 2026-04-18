#include "db/query/fs/File.hpp"
#include "db/Transactions.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/file/Trashed.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/Path.hpp"
#include "runtime/Deps.hpp"
#include "fs/cache/Registry.hpp"
#include "fs/model/stats/Extension.hpp"

#include <optional>

using namespace vh::db::query::fs;
using namespace vh::db::encoding;

unsigned int File::upsertFile(const FilePtr& file) {
    if (!file) throw std::invalid_argument("File cannot be null");
    if (!file->path.string().starts_with("/")) file->setPath("/" + to_utf8_string(file->path.u8string()));

    return Transactions::exec("File::addFile", [&](pqxx::work& txn) {
        const auto exists = txn.exec(pqxx::prepped{"fs_entry_exists_by_inode"}, file->inode).one_field().as<bool>();
        const auto sizeRes = txn.exec(pqxx::prepped{"get_file_size_by_inode"}, file->inode);
        const auto existingSize = sizeRes.empty() ? 0 : sizeRes.one_field().as<uintmax_t>();
        if (file->inode) txn.exec(pqxx::prepped{"delete_fs_entry_by_inode"}, file->inode);

        pqxx::params p;
        p.append(file->vault_id);
        p.append(file->parent_id);
        p.append(file->name);
        p.append(file->base32_alias);
        p.append(file->created_by);
        p.append(file->last_modified_by);
        p.append(to_utf8_string(file->path.u8string()));
        p.append(file->inode);
        p.append(file->mode);
        p.append(file->owner_uid);
        p.append(file->group_gid);
        p.append(file->is_hidden);
        p.append(file->is_system);
        p.append(file->size_bytes);
        p.append(file->mime_type);
        p.append(file->content_hash);
        p.append(file->encryption_iv);

        const auto fileId = txn.exec(pqxx::prepped{"upsert_file_full"}, p).one_row()["fs_entry_id"].as<unsigned int>();

        std::optional<unsigned int> parentId = file->parent_id;
        while (parentId) {
            int64_t size_delta = static_cast<int64_t>(file->size_bytes) - static_cast<int64_t>(existingSize);
            pqxx::params stats_params{parentId, size_delta, exists ? 0 : 1, 0}; // Increment size_bytes and file_count
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto res = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, parentId);
            if (res.empty()) break;
            parentId = res.one_field().as<std::optional<unsigned int>>();
        }

        return fileId;
    });
}

void File::updateFile(const FilePtr& file) {
    if (!file) throw std::invalid_argument("File cannot be null");
    if (file->id == 0) throw std::invalid_argument("File ID cannot be zero");
    if (!file->path.string().starts_with("/")) file->setPath("/" + to_utf8_string(file->path.u8string()));

    Transactions::exec("File::updateFile", [&](pqxx::work& txn) {
        const auto exists = txn.exec(pqxx::prepped{"fs_entry_exists_by_inode"}, file->inode).one_field().as<bool>();
        const auto sizeRes = txn.exec(pqxx::prepped{"get_file_size_by_inode"}, file->inode);
        const auto existingSize = sizeRes.empty() ? 0 : sizeRes.one_field().as<uintmax_t>();

        pqxx::params p;
        p.append(file->id);
        p.append(file->size_bytes);
        p.append(file->mime_type);
        p.append(file->content_hash);
        p.append(file->encryption_iv);

        txn.exec(pqxx::prepped{"update_file_only"}, p);

        std::optional<unsigned int> parentId = file->parent_id;
        while (parentId) {
            int64_t size_delta = static_cast<int64_t>(file->size_bytes) - static_cast<int64_t>(existingSize);
            pqxx::params stats_params{parentId, size_delta, exists ? 0 : 1, 0}; // Increment size_bytes and file_count
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto res = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, parentId);
            if (res.empty()) break;
            parentId = res.one_field().as<std::optional<unsigned int>>();
        }
    });
}

void File::markTrashedFileDeleted(const unsigned int id) {
    Transactions::exec("File::deleteFile", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"mark_trashed_file_deleted_by_id"}, id);
    });
}

void File::deleteFile(const unsigned int userId, const FilePtr& file) {
    Transactions::exec("File::deleteFileByPath", [&](pqxx::work& txn) {
        const auto path = to_utf8_string(file->path.u8string());

        pqxx::params p{file->vault_id, path};
        const auto row = txn.exec(pqxx::prepped{"get_file_parent_id_and_size_by_path"}, p).one_row();
        const auto parentId = row["parent_id"].as<std::optional<unsigned int> >();
        const auto sizeBytes = row["size_bytes"].as<unsigned int>();

        txn.exec(pqxx::prepped{"mark_file_trashed"}, pqxx::params{file->vault_id, path, userId});

        updateParentStatsAndCleanEmptyDirs(txn, parentId, sizeBytes);

        txn.exec(pqxx::prepped{"mark_trashed_file_deleted_fuse_path"}, to_utf8_string(file->fuse_path.u8string()));
    });
}

void File::moveFile(const FilePtr& file, const std::filesystem::path& newPath, unsigned int userId) {
    const auto commonPath = vh::fs::model::common_path_prefix(file->path, newPath);
    Transactions::exec("File::moveFile", [&](pqxx::work& txn) {
        // Update the file's path and parent_id up to the common path
        std::optional<unsigned int> parentId = file->parent_id;
        std::filesystem::path path = file->path;
        while (parentId && path != commonPath) {
            pqxx::params stats_params{parentId, -static_cast<int>(file->size_bytes), -1, 0};
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto row = txn.exec(pqxx::prepped{"get_fs_entry_parent_id_and_path"}, parentId).one_row();
            parentId = row["parent_id"].as<std::optional<unsigned int>>();
            path = row["path"].as<std::string>();
        }

        // update the file's path and parent_id
        file->path = newPath;
        pqxx::params search_params{userId, to_utf8_string(newPath.parent_path().u8string())};
        file->parent_id = txn.exec(pqxx::prepped{"get_fs_entry_id_by_path"}, search_params).one_field().as<unsigned int>();
        file->last_modified_by = userId;

        pqxx::params p;
        p.append(file->vault_id);
        p.append(file->parent_id);
        p.append(file->name);
        p.append(file->created_by);
        p.append(file->last_modified_by);
        p.append(to_utf8_string(file->path.u8string()));
        p.append(file->size_bytes);
        p.append(file->mime_type);
        p.append(file->content_hash);

        txn.exec(pqxx::prepped{"upsert_file_full"}, p);

        // Update parent directories stats
        parentId = file->parent_id;
        path = file->path;
        while (parentId && path != commonPath) {
            pqxx::params stats_params{parentId, file->size_bytes, 1, 0}; // Increment size_bytes and file_count
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto nextRow = txn.exec(pqxx::prepped{"get_fs_entry_parent_id_and_path"}, parentId).one_row();
            parentId = nextRow["parent_id"].as<std::optional<unsigned int> >();
            path = nextRow["path"].as<std::string>();
        }
    });
}

File::FilePtr File::getFileByPath(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("File::getFileByPath", [&](pqxx::work& txn) -> FilePtr {
        const auto res = txn.exec(pqxx::prepped{"get_file_by_path"}, pqxx::params{vaultId, to_utf8_string(relPath.u8string())});
        if (res.empty()) return nullptr;
        const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, res.one_row()["parent_id"].as<std::optional<unsigned int>>());
        return std::make_shared<F>(res.one_row(), parentRows);
    });
}

File::FilePtr File::getFileById(unsigned int id) {
    return Transactions::exec("File::getFileById", [&](pqxx::work& txn) -> FilePtr {
        const auto res = txn.exec(pqxx::prepped{"get_file_by_id"}, id);
        if (res.empty()) return nullptr; // No file found with the given ID
        const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, res.one_row()["parent_id"].as<std::optional<unsigned int>>());
        return std::make_shared<F>(res.one_row(), parentRows);
    });
}


std::string File::getMimeType(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("File::getMimeType", [&](pqxx::work& txn) -> std::string {
        pqxx::params p{vaultId, to_utf8_string(relPath.u8string())};
        const auto res = txn.exec(pqxx::prepped{"get_file_mime_type"}, p);
        if (res.empty()) return "application/octet-stream"; // Default MIME type if not found
        return res.one_field().as<std::string>();
    });
}

bool File::isFile(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("File::isFile", [&](pqxx::work& txn) -> bool {
        pqxx::params p{vaultId, to_utf8_string(relPath.u8string())};
        return txn.exec(pqxx::prepped{"is_file"}, p).one_row()["exists"].as<bool>();
    });
}

std::vector<File::FilePtr> File::listFilesInDir(
    const unsigned int vaultId, const std::filesystem::path& path, const bool recursive) {
    return Transactions::exec("File::listFilesInDir", [&](pqxx::work& txn) {
        const auto patterns = computePatterns(path.string(), recursive);
        const auto res = recursive
                             ? txn.exec(pqxx::prepped{"list_files_in_dir_recursive"}, pqxx::params{vaultId, patterns.like})
                             : txn.exec(pqxx::prepped{"list_files_in_dir"},
                                                 pqxx::params{vaultId, patterns.like, patterns.not_like});

        return vh::fs::model::files_from_pq_res(res);
    });
}

std::vector<File::TrashedFilePtr> File::listTrashedFiles(unsigned int vaultId) {
    return Transactions::exec("File::listTrashedFiles", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"list_trashed_files"}, vaultId);
        return vh::fs::model::file::trashed_files_from_pq_res(res);
    });
}

void File::markFileAsTrashed(const unsigned int userId, const unsigned int vaultId,
                                    const std::filesystem::path& relPath, const bool isFuseCall) {
    const auto file = getFileByPath(vaultId, relPath);
    if (!file) throw std::runtime_error("[markFileAsTrashed] File not found: " + to_utf8_string(relPath.u8string()));

    Transactions::exec("File::markFileAsTrashed", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_file_parent_id_and_size_by_path"},
                                           pqxx::params{vaultId, to_utf8_string(relPath.u8string())});
        if (res.empty()) throw std::runtime_error("[markFileAsTrashed] File not found: " + to_utf8_string(relPath.u8string()));

        const auto row = res[0];
        const auto parentId = row["parent_id"].as<std::optional<unsigned int> >();
        const auto sizeBytes = row["size_bytes"].as<unsigned int>();

        txn.exec(pqxx::prepped{"mark_file_trashed"}, pqxx::params{vaultId, to_utf8_string(relPath.u8string()), userId, to_utf8_string(file->backing_path.u8string())});

        updateParentStatsAndCleanEmptyDirs(txn, parentId, sizeBytes, isFuseCall);
    });
}

void File::markFileAsTrashed(const unsigned int userId, const unsigned int fsId, const bool isFuseCall) {
    const auto file = getFileById(fsId);
    if (!file) throw std::runtime_error("[markFileAsTrashed] File not found with ID: " + std::to_string(fsId));

    Transactions::exec("File::markFileAsTrashed", [&](pqxx::work& txn) {
        const auto row = txn.exec(pqxx::prepped{"get_file_parent_id_and_size"}, fsId).one_row();
        const auto parentId = row["parent_id"].as<std::optional<unsigned int>>();
        const auto sizeBytes = row["size_bytes"].as<unsigned int>();

        pqxx::params p{fsId, userId, to_utf8_string(file->backing_path.u8string())};
        txn.exec(pqxx::prepped{"mark_file_trashed_by_id"}, p);

        updateParentStatsAndCleanEmptyDirs(txn, parentId, sizeBytes, isFuseCall);
    });
}

void File::updateParentStatsAndCleanEmptyDirs(pqxx::work& txn,
                                                     std::optional<unsigned int> parentId,
                                                     const unsigned int sizeBytes,
                                                     const bool isFuseCall) {
    const auto vaultId = txn.exec("SELECT vault_id FROM fs_entry WHERE id = $1", parentId).one_field().as<std::optional<unsigned int>>();
    const auto stopAt = vaultId ?
    txn.exec(pqxx::prepped{"get_vault_root_dir_id_by_vault_id"}, *vaultId).one_field().as<unsigned int>()
    : txn.exec(pqxx::prepped{"get_fs_entry_id_by_path"}, pqxx::params{vaultId, "/"}).one_field().as<unsigned int>();

    int subDirsDeleted = 0;
    bool deleteDirs = !isFuseCall;
    while (parentId) {
        pqxx::params stats_params{parentId, -static_cast<long long>(sizeBytes), -1, subDirsDeleted};
        const auto fsCount = txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params).one_field().as<unsigned int>();
        const auto parentRes = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, *parentId);
        if (*parentId == stopAt) deleteDirs = false;
        if (deleteDirs && fsCount == 0) {
            const auto inode = txn.exec(pqxx::prepped{"get_fs_entry_inode"}, *parentId).one_field().as<ino_t>();
            txn.exec(pqxx::prepped{"delete_fs_entry"}, *parentId);
            runtime::Deps::get().fsCache->evictIno(inode);
            --subDirsDeleted;
        }
        if (parentRes.empty()) break;
        parentId = parentRes.one_field().as<std::optional<unsigned int>>();
    }
}

std::optional<std::pair<std::string, unsigned int>>  File::getEncryptionIVAndVersion(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("File::getEncryptionIV", [&](pqxx::work& txn) -> std::optional<std::pair<std::string, unsigned int>> {
        pqxx::params p{vaultId, to_utf8_string(relPath.u8string())};
        const auto res = txn.exec(pqxx::prepped{"get_file_encryption_iv_and_version"}, p);
        if (res.empty()) return std::nullopt;
        const auto row = res.one_row();
        return std::make_pair(row["encryption_iv"].as<std::string>(), row["encrypted_with_key_version"].as<unsigned int>());
    });
}

void File::setEncryptionIVAndVersion(const FilePtr& f) {
    Transactions::exec("File::setEncryptionIVAndVersion", [&](pqxx::work& txn) {
        pqxx::params p{f->vault_id, to_utf8_string(f->path.u8string()), f->encryption_iv, f->encrypted_with_key_version};
        txn.exec(pqxx::prepped{"set_file_encryption_iv_and_version"}, p);
    });
}

std::string File::getContentHash(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("File::getContentHash", [&](pqxx::work& txn) -> std::string {
        pqxx::params p{vaultId, to_utf8_string(relPath.u8string())};
        return txn.exec(pqxx::prepped{"get_file_content_hash"}, p).one_field().as<std::string>();
    });
}

std::vector<File::FilePtr> File::getFilesOlderThanKeyVersion(unsigned int vaultId, unsigned int keyVersion) {
    return Transactions::exec("File::getFilesOlderThanKeyVersion", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_files_older_than_key_version"}, pqxx::params{vaultId, keyVersion});
        if (res.empty()) return std::vector<FilePtr>();
        return vh::fs::model::files_from_pq_res(res);
    });
}

File::FilePtr File::getLargestFile(unsigned int vaultId) {
    return Transactions::exec("File::getLargestFile", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_n_largest_files"}, pqxx::params{vaultId, 1});
        if (res.empty()) return FilePtr();
        const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, res.one_row()["parent_id"].as<std::optional<unsigned int>>());
        return std::make_shared<F>(res.one_row(), parentRows);
    });
}

std::vector<File::FilePtr> File::getNLargestFiles(unsigned int vaultId, unsigned int n) {
    return Transactions::exec("File::getNLargestFile", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_n_largest_files"}, pqxx::params{vaultId, n});
        if (res.empty()) return std::vector<FilePtr>();
        return vh::fs::model::files_from_pq_res(res);
    });
}

std::vector<File::FilePtr> File::getAllFiles(unsigned int vaultId) {
    return Transactions::exec("File::getAllFiles", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_all_files"}, pqxx::params{vaultId});
        if (res.empty()) return std::vector<FilePtr>();
        return vh::fs::model::files_from_pq_res(res);
    });
}

std::vector<vh::fs::model::stats::Extension> File::getTopExtensionsBySize(const unsigned int vaultId, const unsigned int limit) {
    if (limit == 0) return {};

    return Transactions::exec("File::getTopExtensionsBySize", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(vaultId);
        p.append(limit);

        const auto res = txn.exec(pqxx::prepped{"get_top_extensions_by_size"}, p);
        if (res.empty()) return std::vector<vh::fs::model::stats::Extension>{};

        std::vector<vh::fs::model::stats::Extension> out;
        out.reserve(res.size());

        for (const auto& row : res) {
            vh::fs::model::stats::Extension s{};
            s.extension   = row["ext"].as<std::string>();
            s.total_bytes = row["total_bytes"].as<std::uint64_t>(0);
            out.emplace_back(std::move(s));
        }

        return out;
    });
}
