#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedFsEntries() const {
    conn_->prepare("root_entry_exists",
                   "SELECT EXISTS(SELECT 1 FROM fs_entry "
                   "WHERE parent_id IS NULL AND vault_id IS NULL AND path = '/' AND name = '/') AS exists");

    conn_->prepare("get_root_entry",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.parent_id IS NULL AND fs.vault_id IS NULL AND fs.path = '/' AND fs.name = '/'");

    conn_->prepare("collect_parent_chain",
                   "WITH RECURSIVE parent_chain AS ("
                   "    SELECT id, parent_id, name, base32_alias FROM fs_entry WHERE id = $1 "
                   "    UNION ALL "
                   "    SELECT f.id, f.parent_id, f.name, f.base32_alias FROM fs_entry f "
                   "    JOIN parent_chain pc ON f.id = pc.parent_id "
                   "    WHERE f.parent_id IS NOT NULL "
                   ") "
                   "SELECT * FROM parent_chain ORDER BY parent_id NULLS FIRST");

    conn_->prepare("update_fs_entry_by_inode",
                   R"SQL(
    UPDATE fs_entry
    SET vault_id         = $2,
        parent_id        = $3,
        name             = $4,
        base32_alias     = $5,
        last_modified_by = $6,
        path             = $7,
        mode             = $8,
        owner_uid        = $9,
        group_gid        = $10,
        is_hidden        = $11,
        is_system        = $12,
        updated_at       = CURRENT_TIMESTAMP
    WHERE inode = $1
    RETURNING id
    )SQL");

    conn_->prepare("delete_fs_entry", "DELETE FROM fs_entry WHERE id = $1");

    conn_->prepare("delete_fs_entry_by_path", "DELETE FROM fs_entry WHERE vault_id = $1 AND path = $2");

    conn_->prepare("delete_fs_entry_by_inode", "DELETE FROM fs_entry WHERE inode = $1");

    conn_->prepare("get_fs_entry_by_base32_alias", "SELECT * FROM fs_entry WHERE base32_alias = $1");

    conn_->prepare("get_fs_entry_by_id", "SELECT * FROM fs_entry WHERE id = $1");

    conn_->prepare("get_fs_entry_id_by_base32_alias", "SELECT id FROM fs_entry WHERE base32_alias = $1");

    conn_->prepare("rename_fs_entry",
                   "UPDATE fs_entry SET name = $2, path = $3, base32_alias = $4, parent_id = $5, updated_at = NOW() WHERE id = $1");

    conn_->prepare("get_fs_entry_parent_id", "SELECT parent_id FROM fs_entry WHERE id = $1");

    conn_->prepare("get_fs_entry_parent_id_and_path", "SELECT parent_id, path FROM fs_entry WHERE id = $1");

    conn_->prepare("get_fs_entry_parent_id_by_inode", "SELECT parent_id FROM fs_entry WHERE inode = $1");

    conn_->prepare("get_fs_entry_id_by_path", "SELECT id FROM fs_entry WHERE vault_id = $1 AND path = $2");

    conn_->prepare("fs_entry_exists_by_inode", "SELECT EXISTS(SELECT 1 FROM fs_entry WHERE inode = $1)");

    conn_->prepare("get_next_inode", "SELECT MAX(inode) + 1 FROM fs_entry");

    conn_->prepare("get_base32_alias_from_fs_entry", "SELECT base32_alias FROM fs_entry WHERE id = $1");

    conn_->prepare("get_fs_entry_inode", "SELECT inode FROM fs_entry WHERE id = $1");
}
