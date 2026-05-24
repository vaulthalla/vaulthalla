-- Repair pre-existing sync policies that could cause zero-delay scheduling loops.
UPDATE sync SET interval = 300 WHERE interval <= 0;

ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_list_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_head_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_get_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_put_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_copy_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_delete_requests BIGINT DEFAULT NULL;
ALTER TABLE rsync ADD COLUMN IF NOT EXISTS s3_budget_downloaded_bytes BIGINT DEFAULT NULL;

ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_list_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_head_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_get_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_put_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_copy_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_delete_requests BIGINT NOT NULL DEFAULT 0;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_downloaded_bytes BIGINT NOT NULL DEFAULT 0;

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
    source         VARCHAR(24) NOT NULL DEFAULT 'list_objects_v2'
    CHECK (source IN ('list_objects_v2', 'inventory', 'manifest', 'event')),
    indexed_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id, object_key)
    );

CREATE INDEX IF NOT EXISTS idx_remote_object_index_vault_key
    ON remote_object_index (vault_id, object_key);

CREATE TABLE IF NOT EXISTS remote_manifest_state
(
    vault_id      INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    manifest_key  TEXT NOT NULL,
    etag          TEXT DEFAULT NULL,
    updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (vault_id, manifest_key)
    );
