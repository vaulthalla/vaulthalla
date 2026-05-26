#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSync() const {
    conn_->prepare("insert_sync",
                   "INSERT INTO sync (vault_id, interval) "
                   "VALUES ($1, $2) RETURNING id");

    conn_->prepare("insert_sync_and_fsync",
                   "WITH ins AS ("
                   "  INSERT INTO sync (vault_id, interval) "
                   "  VALUES ($1, $2) RETURNING id"
                   ") "
                   "INSERT INTO fsync (sync_id, conflict_policy) "
                   "SELECT id, $3 FROM ins "
                   "RETURNING sync_id as id");

    conn_->prepare("insert_sync_and_rsync",
                   "WITH ins AS ("
                   "  INSERT INTO sync (vault_id, interval) "
                   "  VALUES ($1, $2) RETURNING id"
                   ") "
                   "INSERT INTO rsync (sync_id, conflict_policy, strategy, "
                   "s3_budget_list_requests, s3_budget_head_requests, s3_budget_get_requests, "
                   "s3_budget_put_requests, s3_budget_copy_requests, s3_budget_delete_requests, "
                   "s3_budget_downloaded_bytes, max_remote_index_age_seconds) "
                   "SELECT id, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12 FROM ins "
                   "RETURNING sync_id as id");

    conn_->prepare("update_sync_and_fsync",
                   "WITH updated_sync AS ("
                   "  UPDATE sync SET interval = $2, enabled = $3, updated_at = NOW() "
                   "  WHERE id = $1 RETURNING id"
                   ") "
                   "UPDATE fsync SET conflict_policy = $4 "
                   "WHERE sync_id = (SELECT id FROM updated_sync)");

    conn_->prepare("update_sync_and_rsync",
                   "WITH updated_sync AS ("
                   "  UPDATE sync SET interval = $2, enabled = $3, updated_at = NOW() "
                   "  WHERE id = $1 RETURNING id"
                   ") "
                   "UPDATE rsync SET strategy = $4, conflict_policy = $5, "
                   "s3_budget_list_requests = $6, "
                   "s3_budget_head_requests = $7, "
                   "s3_budget_get_requests = $8, "
                   "s3_budget_put_requests = $9, "
                   "s3_budget_copy_requests = $10, "
                   "s3_budget_delete_requests = $11, "
                   "s3_budget_downloaded_bytes = $12, "
                   "max_remote_index_age_seconds = $13 "
                   "WHERE sync_id = (SELECT id FROM updated_sync)");

    conn_->prepare("report_sync_started", "UPDATE sync SET last_sync_at = NOW() WHERE id = $1");

    conn_->prepare("report_sync_success",
                   "UPDATE sync SET last_success_at = NOW(), last_sync_at = NOW() WHERE id = $1");

    conn_->prepare("get_fsync_config",
                   "SELECT fs.*, s.* FROM fsync fs JOIN sync s ON s.id = fs.sync_id WHERE vault_id = $1");

    conn_->prepare("get_rsync_config",
                   "SELECT rs.*, s.* FROM rsync rs JOIN sync s ON s.id = rs.sync_id WHERE vault_id = $1");

    conn_->prepare("get_sync_config",
                   "SELECT s.*, rs.*, fs.* "
                   "FROM sync s "
                   "LEFT JOIN rsync rs ON s.id = rs.sync_id "
                   "LEFT JOIN fsync fs ON s.id = fs.sync_id "
                   "WHERE vault_id = $1");

    conn_->prepare("remote_object_index.count_for_vault",
                   "SELECT COUNT(*) FROM remote_object_index WHERE vault_id = $1");

    conn_->prepare("remote_object_index.summary_for_vault",
                   "SELECT COUNT(*) AS object_count, "
                   "CASE WHEN COUNT(*) = 0 THEN NULL "
                   "     WHEN COUNT(DISTINCT source) = 1 THEN MIN(source) "
                   "     ELSE 'mixed' END AS source, "
                   "MAX(indexed_at) AS indexed_at "
                   "FROM remote_object_index WHERE vault_id = $1");

    conn_->prepare("remote_object_index.list_for_vault",
                   "SELECT * FROM remote_object_index WHERE vault_id = $1 ORDER BY object_key");

    conn_->prepare("remote_object_index.delete_for_vault",
                   "DELETE FROM remote_object_index WHERE vault_id = $1");

    conn_->prepare("remote_object_index.delete_key",
                   "DELETE FROM remote_object_index WHERE vault_id = $1 AND object_key = $2");

    conn_->prepare("remote_object_index.upsert",
                   "INSERT INTO remote_object_index "
                   "(vault_id, object_key, size_bytes, last_modified, etag, storage_class, restore_status, "
                   "version_id, event_sequencer, content_hash, encrypted, encryption_iv, encrypted_with_key_version, "
                   "source, indexed_at) "
                   "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, CURRENT_TIMESTAMP) "
                   "ON CONFLICT (vault_id, object_key) DO UPDATE SET "
                   "size_bytes = EXCLUDED.size_bytes, "
                   "last_modified = EXCLUDED.last_modified, "
                   "etag = EXCLUDED.etag, "
                   "storage_class = EXCLUDED.storage_class, "
                   "restore_status = EXCLUDED.restore_status, "
                   "version_id = EXCLUDED.version_id, "
                   "event_sequencer = COALESCE(EXCLUDED.event_sequencer, remote_object_index.event_sequencer), "
                   "content_hash = CASE "
                   "  WHEN EXCLUDED.content_hash IS NOT NULL THEN EXCLUDED.content_hash "
                   "  WHEN EXCLUDED.etag IS NOT DISTINCT FROM remote_object_index.etag "
                   "   AND EXCLUDED.version_id IS NOT DISTINCT FROM remote_object_index.version_id "
                   "   AND EXCLUDED.size_bytes = remote_object_index.size_bytes "
                   "   AND EXCLUDED.last_modified IS NOT DISTINCT FROM remote_object_index.last_modified "
                   "  THEN remote_object_index.content_hash ELSE NULL END, "
                   "encrypted = CASE "
                   "  WHEN EXCLUDED.encrypted IS NOT NULL THEN EXCLUDED.encrypted "
                   "  WHEN EXCLUDED.etag IS NOT DISTINCT FROM remote_object_index.etag "
                   "   AND EXCLUDED.version_id IS NOT DISTINCT FROM remote_object_index.version_id "
                   "   AND EXCLUDED.size_bytes = remote_object_index.size_bytes "
                   "   AND EXCLUDED.last_modified IS NOT DISTINCT FROM remote_object_index.last_modified "
                   "  THEN remote_object_index.encrypted ELSE NULL END, "
                   "encryption_iv = CASE "
                   "  WHEN EXCLUDED.encryption_iv IS NOT NULL THEN EXCLUDED.encryption_iv "
                   "  WHEN EXCLUDED.etag IS NOT DISTINCT FROM remote_object_index.etag "
                   "   AND EXCLUDED.version_id IS NOT DISTINCT FROM remote_object_index.version_id "
                   "   AND EXCLUDED.size_bytes = remote_object_index.size_bytes "
                   "   AND EXCLUDED.last_modified IS NOT DISTINCT FROM remote_object_index.last_modified "
                   "  THEN remote_object_index.encryption_iv ELSE NULL END, "
                   "encrypted_with_key_version = CASE "
                   "  WHEN EXCLUDED.encrypted_with_key_version IS NOT NULL THEN EXCLUDED.encrypted_with_key_version "
                   "  WHEN EXCLUDED.etag IS NOT DISTINCT FROM remote_object_index.etag "
                   "   AND EXCLUDED.version_id IS NOT DISTINCT FROM remote_object_index.version_id "
                   "   AND EXCLUDED.size_bytes = remote_object_index.size_bytes "
                   "   AND EXCLUDED.last_modified IS NOT DISTINCT FROM remote_object_index.last_modified "
                   "  THEN remote_object_index.encrypted_with_key_version ELSE NULL END, "
                   "source = EXCLUDED.source, "
                   "indexed_at = CURRENT_TIMESTAMP "
                   "WHERE EXCLUDED.event_sequencer IS NULL "
                   "   OR remote_object_index.event_sequencer IS NULL "
                   "   OR LENGTH(LTRIM(EXCLUDED.event_sequencer, '0')) > LENGTH(LTRIM(remote_object_index.event_sequencer, '0')) "
                   "   OR (LENGTH(LTRIM(EXCLUDED.event_sequencer, '0')) = LENGTH(LTRIM(remote_object_index.event_sequencer, '0')) "
                   "       AND LOWER(LTRIM(EXCLUDED.event_sequencer, '0')) >= LOWER(LTRIM(remote_object_index.event_sequencer, '0'))) "
                   "RETURNING id");

    conn_->prepare("remote_object_index.delete_key_if_not_stale_event",
                   "DELETE FROM remote_object_index "
                   "WHERE vault_id = $1 AND object_key = $2 "
                   "  AND ($3::text IS NULL "
                   "       OR event_sequencer IS NULL "
                   "       OR LENGTH(LTRIM($3::text, '0')) > LENGTH(LTRIM(event_sequencer, '0')) "
                   "       OR (LENGTH(LTRIM($3::text, '0')) = LENGTH(LTRIM(event_sequencer, '0')) "
                   "           AND LOWER(LTRIM($3::text, '0')) >= LOWER(LTRIM(event_sequencer, '0'))))");

    conn_->prepare("remote_manifest_state.get_etag",
                   "SELECT etag FROM remote_manifest_state WHERE vault_id = $1 AND manifest_key = $2");

    conn_->prepare("remote_manifest_state.get",
                   "SELECT etag, generated_at, object_count, object_checksum, updated_at "
                   "FROM remote_manifest_state WHERE vault_id = $1 AND manifest_key = $2");

    conn_->prepare("remote_manifest_state.upsert",
                   "INSERT INTO remote_manifest_state "
                   "(vault_id, manifest_key, etag, generated_at, object_count, object_checksum, updated_at) "
                   "VALUES ($1, $2, $3, $4, $5, $6, CURRENT_TIMESTAMP) "
                   "ON CONFLICT (vault_id, manifest_key) DO UPDATE SET "
                   "etag = EXCLUDED.etag, "
                   "generated_at = COALESCE(EXCLUDED.generated_at, remote_manifest_state.generated_at), "
                   "object_count = COALESCE(EXCLUDED.object_count, remote_manifest_state.object_count), "
                   "object_checksum = COALESCE(EXCLUDED.object_checksum, remote_manifest_state.object_checksum), "
                   "updated_at = CURRENT_TIMESTAMP");
}
