#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedVaults() const {
    conn_->prepare("upsert_vault",
                   R"(INSERT INTO vault (name, type, description, owner_id, mount_point, quota, is_active)
       VALUES ($1, $2, $3, $4, $5, $6, $7)
       ON CONFLICT (name, owner_id)
       DO UPDATE SET
           type = EXCLUDED.type,
           description = EXCLUDED.description,
           quota = EXCLUDED.quota,
           is_active = EXCLUDED.is_active,
           mount_point = EXCLUDED.mount_point
       RETURNING id)");

    conn_->prepare("upsert_s3_vault",
                   "INSERT INTO s3 (vault_id, api_key_id, bucket, encrypt_upstream) VALUES ($1, $2, $3, $4) "
                   "ON CONFLICT (api_key_id, bucket) DO UPDATE "
                   "SET bucket = EXCLUDED.bucket, api_key_id = EXCLUDED.api_key_id, "
                   "encrypt_upstream = EXCLUDED.encrypt_upstream, vault_id = EXCLUDED.vault_id "
                   "RETURNING vault_id");

    conn_->prepare("insert_s3_vault", "INSERT INTO s3 (vault_id, api_key_id, bucket) VALUES ($1, $2, $3) "
                   "ON CONFLICT (vault_id) DO NOTHING");

    conn_->prepare("vault_root_exists",
                   "SELECT EXISTS(SELECT 1 FROM fs_entry WHERE vault_id = $1 AND path = '/') AS exists");

    conn_->prepare("get_vault",
                   "SELECT v.*, s.* "
                   "FROM vault v "
                   "LEFT JOIN s3 s ON v.id = s.vault_id "
                   "WHERE v.id = $1");

    conn_->prepare("get_vault_by_name_and_owner",
                   "SELECT v.*, s.* "
                   "FROM vault v "
                   "LEFT JOIN s3 s ON v.id = s.vault_id "
                   "WHERE v.name = $1 AND v.owner_id = $2");

    conn_->prepare("get_vault_owners_name",
                   "SELECT u.name FROM users u "
                   "JOIN vault v ON u.id = v.owner_id "
                   "WHERE v.id = $1");

    conn_->prepare("get_vault_owner_id", "SELECT owner_id FROM vault WHERE id = $1");

    conn_->prepare("get_max_vault_id", "SELECT MAX(id) FROM vault");

    conn_->prepare("get_vault_root_dir_id_by_vault_id", "SELECT id FROM fs_entry WHERE vault_id = $1 AND path = '/'");

    conn_->prepare("vault_exists", "SELECT EXISTS(SELECT 1 FROM vault WHERE name = $1 AND owner_id = $2) AS exists");

    conn_->prepare("is_s3_vault", "SELECT EXISTS(SELECT 1 FROM s3 WHERE vault_id = $1) AS is_s3");

    conn_->prepare("get_vault_mount_point", "SELECT mount_point FROM vault WHERE id = $1");
}
