-- Repair pre-existing sync policies that could cause zero-delay scheduling loops.
UPDATE sync SET interval = 300 WHERE interval <= 0;

ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_list_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_head_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_get_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_put_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_copy_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_delete_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_downloaded_bytes BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS max_remote_index_age_seconds BIGINT DEFAULT 86400;
UPDATE rsync SET max_remote_index_age_seconds = 86400 WHERE max_remote_index_age_seconds IS NULL;

ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_list_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_head_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_get_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_put_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_copy_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_delete_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_downloaded_bytes BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_list_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_head_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_get_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_put_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_copy_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_delete_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_body_download_bytes BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_estimated_upload_bytes BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_remote_index_objects BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_archive_downloads_skipped BIGINT NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS remote_object_index
(
    id             SERIAL PRIMARY KEY,
    vault_id       INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    object_key     TEXT NOT NULL,
    size_bytes     BIGINT NOT NULL DEFAULT 0,
    last_modified  TIMESTAMP DEFAULT NULL,
    etag           TEXT DEFAULT NULL,
    storage_class  TEXT DEFAULT NULL,
    restore_status TEXT DEFAULT NULL,
    version_id     TEXT DEFAULT NULL,
    event_sequencer TEXT DEFAULT NULL,
    source         VARCHAR(24) NOT NULL DEFAULT 'list_objects_v2'
    CHECK (source IN ('list_objects_v2', 'inventory', 'manifest', 'event')),
    indexed_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id, object_key)
    );

CREATE INDEX IF NOT EXISTS idx_remote_object_index_vault_key
    ON remote_object_index (vault_id, object_key);

ALTER TABLE remote_object_index ADD COLUMN IF NOT EXISTS version_id TEXT DEFAULT NULL;
ALTER TABLE remote_object_index ADD COLUMN IF NOT EXISTS event_sequencer TEXT DEFAULT NULL;

CREATE TABLE IF NOT EXISTS remote_manifest_state
(
    vault_id      INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    manifest_key  TEXT NOT NULL,
    etag          TEXT DEFAULT NULL,
    generated_at  TIMESTAMP DEFAULT NULL,
    object_count  BIGINT DEFAULT NULL,
    object_checksum TEXT DEFAULT NULL,
    updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (vault_id, manifest_key)
    );

ALTER TABLE remote_manifest_state ADD COLUMN IF NOT EXISTS generated_at TIMESTAMP DEFAULT NULL;
ALTER TABLE remote_manifest_state ADD COLUMN IF NOT EXISTS object_count BIGINT DEFAULT NULL;
ALTER TABLE remote_manifest_state ADD COLUMN IF NOT EXISTS object_checksum TEXT DEFAULT NULL;

ALTER TABLE sync_throughput DROP CONSTRAINT IF EXISTS sync_throughput_metric_type_check;
ALTER TABLE sync_throughput
    ADD CONSTRAINT sync_throughput_metric_type_check
    CHECK (metric_type IN ('upload', 'download', 'index', 'rename', 'copy', 'delete'));
