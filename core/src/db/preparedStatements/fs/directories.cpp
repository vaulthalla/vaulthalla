#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedDirectories() const {
    conn_->prepare("update_dir_stats",
                   "UPDATE directories "
                   "SET size_bytes = size_bytes + $2, file_count = file_count + $3, subdirectory_count = subdirectory_count + $4 "
                   "WHERE fs_entry_id = $1 RETURNING file_count");

    conn_->prepare("get_dir_file_count", "SELECT file_count FROM directories WHERE fs_entry_id = $1");

    conn_->prepare("is_dir_empty",
                   "SELECT NOT EXISTS (SELECT 1 FROM fs_entry WHERE parent_id = $1 AND id != 1) AS is_empty");

    conn_->prepare("delete_empty_dir",
                   "DELETE FROM directories WHERE fs_entry_id = $1 AND file_count = 0 AND subdirectory_count = 0");

    conn_->prepare(
        "upsert_directory",
        "WITH inserted AS ( "
        "  INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, inode, mode, owner_uid, group_gid, is_hidden, is_system) "
        "  VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
        "  ON CONFLICT (parent_id, name) DO UPDATE SET "
        "    last_modified_by = EXCLUDED.last_modified_by, "
        "    inode = EXCLUDED.inode, "
        "    mode = EXCLUDED.mode, "
        "    owner_uid = EXCLUDED.owner_uid, "
        "    group_gid = EXCLUDED.group_gid, "
        "    is_hidden = EXCLUDED.is_hidden, "
        "    is_system = EXCLUDED.is_system, "
        "    updated_at = NOW() "
        "  RETURNING id "
        ") "
        "INSERT INTO directories (fs_entry_id, size_bytes, file_count, subdirectory_count) "
        "SELECT id, $14, $15, $16 FROM inserted "
        "ON CONFLICT (fs_entry_id) DO UPDATE SET "
        "  size_bytes = EXCLUDED.size_bytes, "
        "  file_count = EXCLUDED.file_count, "
        "  subdirectory_count = EXCLUDED.subdirectory_count "
        "RETURNING fs_entry_id"
    );

    conn_->prepare("list_dirs_in_dir_by_parent_id",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.parent_id = $1 AND fs.id != 1");

    conn_->prepare("list_dirs_in_dir_by_parent_id_recursive",
                   "WITH RECURSIVE dir_tree AS ("
                   "    SELECT fs.*, d.* "
                   "    FROM fs_entry fs "
                   "    JOIN directories d ON fs.id = d.fs_entry_id "
                   "    WHERE fs.parent_id = $1 "
                   "  UNION ALL "
                   "    SELECT fs2.*, d2.* "
                   "    FROM fs_entry fs2 "
                   "    JOIN directories d2 ON fs2.id = d2.fs_entry_id "
                   "    JOIN dir_tree dt ON fs2.parent_id = dt.id "
                   ") "
                   "SELECT * FROM dir_tree");

    conn_->prepare("get_dir_by_id",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.id = $1");

    conn_->prepare("get_dir_by_path",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.vault_id = $1 AND fs.path = $2");

    conn_->prepare("get_dir_by_base32_alias",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.base32_alias = $1");

    conn_->prepare("get_dir_by_inode",
                   "SELECT fs.*, d.* "
                   "FROM fs_entry fs "
                   "JOIN directories d ON fs.id = d.fs_entry_id "
                   "WHERE fs.inode = $1");

    conn_->prepare("is_directory",
                   "SELECT EXISTS (SELECT 1 FROM directories d "
                   "JOIN fs_entry fs ON d.fs_entry_id = fs.id "
                   "WHERE fs.vault_id = $1 AND fs.path = $2)");

    conn_->prepare("collect_parent_dir_stats",
                   "WITH RECURSIVE parent_chain AS ( "
                   "  SELECT "
                   "    d.fs_entry_id        AS id, "
                   "    f.parent_id, "
                   "    f.name, "
                   "    f.base32_alias, "
                   "    d.size_bytes, "
                   "    d.file_count, "
                   "    d.subdirectory_count "
                   "  FROM directories d "
                   "  JOIN fs_entry f ON f.id = d.fs_entry_id "
                   "  WHERE d.fs_entry_id = $1 "
                   "  UNION ALL "
                   "  SELECT "
                   "    d.fs_entry_id        AS id, "
                   "    f.parent_id, "
                   "    f.name, "
                   "    f.base32_alias, "
                   "    d.size_bytes, "
                   "    d.file_count, "
                   "    d.subdirectory_count "
                   "  FROM directories d "
                   "  JOIN fs_entry f ON f.id = d.fs_entry_id "
                   "  JOIN parent_chain pc ON d.fs_entry_id = pc.parent_id "
                   "  WHERE pc.parent_id IS NOT NULL "
                   ") "
                   "SELECT * FROM parent_chain "
                   "ORDER BY parent_id NULLS FIRST");
}
