#include "db/query/fs/Directory.hpp"
#include "db/Transactions.hpp"
#include "fs/model/Directory.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/Path.hpp"

#include <optional>

using namespace vh::db::query::fs;
using namespace vh::db::encoding;

unsigned int Directory::upsertDirectory(const DirPtr& directory) {
    if (!directory->path.string().starts_with("/")) directory->setPath("/" + to_utf8_string(directory->path.u8string()));
    return Transactions::exec("Directory::addDirectory", [&](pqxx::work& txn) {
        const auto exists = txn.exec(pqxx::prepped{"fs_entry_exists_by_inode"}, directory->inode).one_field().as<bool>();
        if (directory->inode && *directory->inode != 1) txn.exec(pqxx::prepped{"delete_fs_entry_by_inode"}, directory->inode);

        pqxx::params p;
        p.append(directory->vault_id);
        p.append(directory->parent_id);
        p.append(directory->name);
        p.append(directory->base32_alias);
        p.append(directory->created_by);
        p.append(directory->last_modified_by);
        p.append(to_utf8_string(directory->path.u8string()));
        p.append(directory->inode);
        p.append(directory->mode);
        p.append(directory->owner_uid);
        p.append(directory->group_gid);
        p.append(directory->is_hidden);
        p.append(directory->is_system);
        p.append(directory->size_bytes);
        p.append(directory->file_count);
        p.append(directory->subdirectory_count);

        const auto id = txn.exec(pqxx::prepped{"upsert_directory"}, p).one_field().as<unsigned int>();

        if (directory->parent_id) {
            std::optional<unsigned int> parentId = directory->parent_id;
            while (parentId) {
                pqxx::params stats_params{parentId, 0, 0, exists ? 0 : 1}; // Increment subdir count
                txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
                const auto res = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, parentId);
                if (res.empty()) break;
                parentId = res.one_field().as<std::optional<unsigned int>>();
            }
        }

        return id;
    });
}

bool Directory::isDirectoryEmpty(unsigned int id) {
    return Transactions::exec("Directory::isDirectoryEmpty", [&](pqxx::work& txn) -> bool {
        const auto res = txn.exec(pqxx::prepped{"is_dir_empty"}, id);
        return res.one_field().as<bool>();
    });
}

void Directory::deleteEmptyDirectory(const unsigned int id) {
    Transactions::exec("Directory::deleteDirectory", [&](pqxx::work& txn) {
        const auto parent = txn.exec(pqxx::prepped{"get_fs_entry_parent_id"}, id).one_field().as<std::optional<unsigned int>>();
        if (parent) txn.exec(pqxx::prepped{"update_dir_stats"}, pqxx::params{parent, 0, 0, -1});
        txn.exec(pqxx::prepped{"delete_fs_entry"}, pqxx::params{id});
    });
}

void Directory::moveDirectory(const DirPtr& directory, const std::filesystem::path& newPath, unsigned int userId) {
    if (!directory) throw std::invalid_argument("Directory cannot be null");
    if (!newPath.string().starts_with("/")) throw std::invalid_argument("New path must start with '/'");

    const auto commonPath = vh::fs::model::common_path_prefix(directory->path, newPath);

    Transactions::exec("Directory::moveDirectory", [&](pqxx::work& txn) {
        // update parents of the directory up to the common path
        std::optional<unsigned int> parentId = directory->parent_id;
        std::filesystem::path path = directory->path;
        while (parentId && path != commonPath) {
            pqxx::params stats_params{parentId, -static_cast<int>(directory->size_bytes), -static_cast<int>(directory->file_count), 0};
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto row = txn.exec(pqxx::prepped{"get_fs_entry_parent_id_and_path"}, parentId).one_row();
            parentId = row["parent_id"].as<std::optional<unsigned int>>();
            path = row["path"].as<std::string>();
        }

        // Update the directory's path and parent_id
        directory->path = newPath;
        pqxx::params search_params{userId, to_utf8_string(newPath.parent_path().u8string())};
        directory->parent_id = txn.exec(pqxx::prepped{"get_fs_entry_id_by_path"}, search_params).one_field().as<unsigned int>();
        directory->last_modified_by = userId;

        pqxx::params p;
        p.append(directory->vault_id);
        p.append(directory->parent_id);
        p.append(directory->name);
        p.append(directory->created_by);
        p.append(directory->last_modified_by);
        p.append(to_utf8_string(directory->path.u8string()));
        p.append(directory->size_bytes);
        p.append(directory->file_count);
        p.append(directory->subdirectory_count);

        txn.exec(pqxx::prepped{"upsert_directory"}, p);

        // Update parent directories stats
        parentId = directory->parent_id;
        path = directory->path;
        while (parentId && path != commonPath) {
            pqxx::params stats_params{parentId, directory->size_bytes, directory->file_count, 0}; // Increment size_bytes and file_count
            txn.exec(pqxx::prepped{"update_dir_stats"}, stats_params);
            const auto nextRow = txn.exec(pqxx::prepped{"get_fs_entry_parent_id_and_path"}, parentId).one_row();
            parentId = nextRow["parent_id"].as<std::optional<unsigned int>>();
            path = nextRow["path"].as<std::string>();
        }
    });
}

Directory::DirPtr Directory::getDirectoryByPath(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("Directory::getDirectoryByPath", [&](pqxx::work& txn) {
        const auto row = txn.exec(pqxx::prepped{"get_dir_by_path"}, pqxx::params{vaultId, relPath.string()}).one_row();
        const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, row["parent_id"].as<std::optional<unsigned int>>());
        return std::make_shared<Dir>(row, parentRows);
    });
}

std::optional<unsigned int> Directory::getDirectoryIdByPath(const unsigned int vaultId, const std::filesystem::path& path) {
    return Transactions::exec("Directory::getDirectoryIdByPath", [&](pqxx::work& txn) -> std::optional<unsigned int> {
        pqxx::params p{vaultId, path.string()};
        const auto res = txn.exec(pqxx::prepped{"get_fs_entry_id_by_path"}, p);
        if (res.empty()) return std::nullopt;
        return res.one_row()["id"].as<unsigned int>();
    });
}

unsigned int Directory::getRootDirectoryId(const unsigned int vaultId) {
    return Transactions::exec("Directory::getRootDirectoryId", [&](pqxx::work& txn) -> unsigned int {
        pqxx::params p{vaultId, "/"};
        return txn.exec(pqxx::prepped{"get_fs_entry_id_by_path"}, p).one_row()["id"].as<unsigned int>();
    });
}

bool Directory::isDirectory(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return Transactions::exec("Directory::isDirectory", [&](pqxx::work& txn) -> bool {
        pqxx::params p{vaultId, relPath.string()};
        return txn.exec(pqxx::prepped{"is_directory"}, p).one_row()["exists"].as<bool>();
    });
}

bool Directory::directoryExists(const unsigned int vaultId, const std::filesystem::path& relPath) {
    return isDirectory(vaultId, relPath);
}

std::vector<Directory::DirPtr> Directory::listDirectoriesInDir(const unsigned int parentId, const bool recursive) {
    return Transactions::exec("Directory::listDirectoriesInDir", [&](pqxx::work& txn) {
        const auto res = recursive
            ? txn.exec(pqxx::prepped{"list_dirs_in_dir_by_parent_id_recursive"}, parentId)
            : txn.exec(pqxx::prepped{"list_dirs_in_dir_by_parent_id"}, parentId);

        return vh::fs::model::directories_from_pq_res(res);
    });
}

pqxx::result Directory::collectParentStats(unsigned int parentId) {
    return Transactions::exec("Directory::collectParentStats", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"collect_parent_dir_stats"}, parentId);
    });
}
