#include "db/query/fs/Entry.hpp"
#include "db/Transactions.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Directory.hpp"
#include "db/encoding/u8.hpp"
#include "log/Registry.hpp"

using namespace vh::db::query::fs;
using namespace vh::db::encoding;

bool Entry::rootExists() {
    return Transactions::exec("Entry::rootExists", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"root_entry_exists"}).one_field().as<bool>();
    });
}

Entry::EntryPtr Entry::getRootEntry() {
    return Transactions::exec("Entry::getRootEntry", [&](pqxx::work& txn) -> EntryPtr {
        const auto res = txn.exec(pqxx::prepped{"get_root_entry"});
        if (res.empty()) {
            log::Registry::db()->warn("[Entry::getRootEntry] No root entry found in the database");
            return nullptr;
        }
        return std::make_shared<vh::fs::model::Directory>(res.one_row(), pqxx::result{});
    });
}

void Entry::updateFSEntry(const EntryPtr& entry) {
    Transactions::exec("Entry::updateFSEntry", [&](pqxx::work& txn) {
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
    });
}

Entry::EntryPtr Entry::getFSEntry(const std::string& base32) {
    return Transactions::exec("Entry::getFSEntry", [&](pqxx::work& txn) -> Entry::EntryPtr {
        const auto fileRes = txn.exec(pqxx::prepped{"get_file_by_base32_alias"}, base32);
        if (!fileRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, fileRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::File>(fileRes.one_row(), parentRows);
        }

        const auto dirRes = txn.exec(pqxx::prepped{"get_dir_by_base32_alias"}, base32);
        if (!dirRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, dirRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::Directory>(dirRes.one_row(), parentRows);
        }

        return nullptr;
    });
}

Entry::EntryPtr Entry::getFSEntryByInode(const ino_t ino) {
    return Transactions::exec("Entry::getFSEntryByInode", [&](pqxx::work& txn) -> Entry::EntryPtr {
        const auto fileRes = txn.exec(pqxx::prepped{"get_file_by_inode"}, ino);
        if (!fileRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, fileRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::File>(fileRes.one_row(), parentRows);
        }

        const auto dirRes = txn.exec(pqxx::prepped{"get_dir_by_inode"}, ino);
        if (!dirRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, dirRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::Directory>(dirRes.one_row(), parentRows);
        }

        return nullptr;
    });
}


Entry::EntryPtr Entry::getFSEntryById(unsigned int entryId) {
    return Transactions::exec("Entry::getFSEntryById", [&](pqxx::work& txn) -> Entry::EntryPtr {
        const auto res = txn.exec(pqxx::prepped{"get_fs_entry_by_id"}, entryId);
        if (res.empty()) return nullptr;

        const auto fileRes = txn.exec(pqxx::prepped{"get_file_by_id"}, entryId);
        if (!fileRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, fileRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::File>(fileRes.one_row(), parentRows);
        }

        const auto dirRes = txn.exec(pqxx::prepped{"get_dir_by_id"}, entryId);
        if (!dirRes.empty()) {
            const auto parentRows = txn.exec(pqxx::prepped{"collect_parent_chain"}, dirRes.one_row()["parent_id"].as<std::optional<unsigned int>>());
            return std::make_shared<vh::fs::model::Directory>(dirRes.one_row(), parentRows);
        }

        return nullptr;
    });
}

void Entry::renameEntry(const EntryPtr& entry) {
    if (entry->parent_id && *entry->parent_id == 0) entry->parent_id = std::nullopt;
    Transactions::exec("Entry::renameEntry", [&](pqxx::work& txn) {
        pqxx::params p{entry->id, entry->name,
                       to_utf8_string(entry->path.u8string()),
                       to_utf8_string(entry->fuse_path.u8string()),
                       entry->parent_id};
        txn.exec(pqxx::prepped{"rename_fs_entry"}, p);
    });
}

std::vector<Entry::EntryPtr> Entry::listDir(const std::optional<unsigned int>& entryId, const bool recursive) {
    if (!entryId) {
        log::Registry::db()->warn("[Entry::listDir] entryId is null, returning empty list");
        return {};
    }

    log::Registry::db()->debug("[Entry::listDir] Listing directory with parent ID: {}, recursive: {}", *entryId, recursive);

    return Transactions::exec("Entry::listDirById", [&](pqxx::work& txn) {
        const auto files = vh::fs::model::files_from_pq_res(
            recursive
                ? txn.exec(pqxx::prepped{"list_files_in_dir_by_parent_id_recursive"}, entryId)
                : txn.exec(pqxx::prepped{"list_files_in_dir_by_parent_id"}, entryId)
            );

        log::Registry::db()->info("[Entry::listDir] Executing queries for directories...");
        const auto directories = vh::fs::model::directories_from_pq_res(
            recursive
                ? txn.exec(pqxx::prepped{"list_dirs_in_dir_by_parent_id_recursive"}, entryId)
                : txn.exec(pqxx::prepped{"list_dirs_in_dir_by_parent_id"}, entryId)
            );

        return merge_entries(files, directories);
    });
}

ino_t Entry::getNextInode() {
    return Transactions::exec("Entry::getNextInode", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"get_next_inode"});
        if (res.empty()) throw std::runtime_error("No inode available");
        return res.one_field().as<ino_t>();
    });
}

pqxx::result Entry::collectParentChain(unsigned int parentId) {
    return Transactions::exec("Entry::collectParentChain", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"collect_parent_chain"}, parentId);
    });
}

