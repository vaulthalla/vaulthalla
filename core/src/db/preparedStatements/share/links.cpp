#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareLinks() const {
    conn_->prepare("share_link_insert", R"SQL(
        INSERT INTO share_link (
            token_lookup_id, token_hash, created_by, vault_id, root_entry_id, root_path,
            target_type, link_type, access_mode, allowed_ops, name, public_label, description,
            expires_at, max_downloads, max_upload_files, max_upload_size_bytes, max_upload_total_bytes,
            duplicate_policy, allowed_mime_types, blocked_mime_types, allowed_extensions, blocked_extensions, metadata
        )
        VALUES (
            $1, $2, $3, $4, $5, $6,
            $7, $8, $9, $10, $11, $12, $13,
            $14, $15, $16, $17, $18,
            $19, $20, $21, $22, $23, $24::jsonb
        )
        RETURNING *
    )SQL");

    conn_->prepare("share_link_get", "SELECT * FROM share_link WHERE id = $1");
    conn_->prepare("share_link_get_by_lookup_id", "SELECT * FROM share_link WHERE token_lookup_id = $1");
    conn_->prepare("share_link_list_for_user", "SELECT * FROM share_link WHERE created_by = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3");
    conn_->prepare("share_link_list_for_vault", "SELECT * FROM share_link WHERE vault_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3");
    conn_->prepare("share_link_list_for_target", "SELECT * FROM share_link WHERE root_entry_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3");

    conn_->prepare("share_link_update", R"SQL(
        UPDATE share_link
        SET updated_by = $2,
            target_type = $3,
            link_type = $4,
            access_mode = $5,
            allowed_ops = $6,
            name = $7,
            public_label = $8,
            description = $9,
            expires_at = $10,
            disabled_at = $11,
            max_downloads = $12,
            max_upload_files = $13,
            max_upload_size_bytes = $14,
            max_upload_total_bytes = $15,
            duplicate_policy = $16,
            allowed_mime_types = $17,
            blocked_mime_types = $18,
            allowed_extensions = $19,
            blocked_extensions = $20,
            metadata = $21::jsonb
        WHERE id = $1
        RETURNING *
    )SQL");

    conn_->prepare("share_link_revoke", "UPDATE share_link SET revoked_at = CURRENT_TIMESTAMP, revoked_by = $2 WHERE id = $1 RETURNING id");
    conn_->prepare("share_link_rotate_token", "UPDATE share_link SET token_lookup_id = $2, token_hash = $3, updated_by = $4 WHERE id = $1 RETURNING id");
    conn_->prepare("share_link_touch_access", "UPDATE share_link SET last_accessed_at = CURRENT_TIMESTAMP, access_count = access_count + 1 WHERE id = $1 RETURNING id");
    conn_->prepare("share_link_increment_download", "UPDATE share_link SET download_count = download_count + 1 WHERE id = $1 RETURNING id");
    conn_->prepare("share_link_increment_upload", "UPDATE share_link SET upload_count = upload_count + 1 WHERE id = $1 RETURNING id");
}
