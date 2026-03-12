-- ##################################
-- Sync & Backup Schema
-- ##################################

CREATE TABLE IF NOT EXISTS backup_policy
(
    id              SERIAL PRIMARY KEY,
    vault_id        INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    backup_interval BIGINT NOT NULL DEFAULT 86400,
    last_backup_at  TIMESTAMP DEFAULT NULL,
    last_success_at TIMESTAMP DEFAULT NULL,
    retention       INTERVAL DEFAULT NULL,
    retry_count     INTEGER NOT NULL DEFAULT 0,
    enabled         BOOLEAN NOT NULL DEFAULT FALSE,
    last_error      TEXT DEFAULT NULL,
    status          VARCHAR(12) DEFAULT 'idle'
    CHECK (status IN ('idle', 'syncing', 'error'))
    );

CREATE TABLE IF NOT EXISTS sync
(
    id              SERIAL PRIMARY KEY,
    vault_id        INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    interval        BIGINT NOT NULL DEFAULT 300,
    enabled         BOOLEAN NOT NULL DEFAULT TRUE,
    last_sync_at    TIMESTAMP DEFAULT NULL,
    last_success_at TIMESTAMP DEFAULT NULL,
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id)
    );

CREATE TABLE IF NOT EXISTS fsync
(
    sync_id         INTEGER PRIMARY KEY REFERENCES sync (id) ON DELETE CASCADE,
    conflict_policy VARCHAR(12) DEFAULT 'keep_both'
    CHECK (conflict_policy IN ('overwrite', 'keep_both', 'ask'))
    );

CREATE TABLE IF NOT EXISTS rsync
(
    sync_id         INTEGER PRIMARY KEY REFERENCES sync (id) ON DELETE CASCADE,
    strategy        VARCHAR(8) DEFAULT 'cache'
    CHECK (strategy IN ('cache', 'sync', 'mirror')),
    conflict_policy VARCHAR(12) DEFAULT 'keep_local'
    CHECK (conflict_policy IN ('keep_local', 'keep_remote', 'keep_newest', 'ask'))
    );

-- -----------------------------------
-- Sync Event (per-run health + audit)
-- -----------------------------------
CREATE TABLE IF NOT EXISTS sync_event
(
    id               SERIAL PRIMARY KEY,
    vault_id          INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,

    -- stable identity for a "run"
    run_uuid          UUID NOT NULL DEFAULT gen_random_uuid(),

    -- core timing
    timestamp_begin   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    timestamp_end     TIMESTAMP DEFAULT NULL,

    -- run state
    status            VARCHAR(12) NOT NULL DEFAULT 'running'
    CHECK (status IN ('pending', 'running', 'success', 'stalled', 'error', 'cancelled')),
    trigger           VARCHAR(12) NOT NULL DEFAULT 'schedule'
    CHECK (trigger IN ('schedule', 'manual', 'startup', 'webhook', 'retry')),
    retry_attempt     INTEGER NOT NULL DEFAULT 0,

    -- stall/error diagnostics
    heartbeat_at      TIMESTAMP DEFAULT NULL,
    stall_reason      TEXT DEFAULT NULL,
    error_code        TEXT DEFAULT NULL,
    error_message     TEXT DEFAULT NULL,

    -- summary counters (so dashboards don't need 4 joins)
    num_ops_total     BIGINT NOT NULL DEFAULT 0,
    num_failed_ops    BIGINT NOT NULL DEFAULT 0,
    num_conflicts     BIGINT NOT NULL DEFAULT 0,
    bytes_up          BIGINT NOT NULL DEFAULT 0,
    bytes_down        BIGINT NOT NULL DEFAULT 0,

    -- divergence / watermarks (optional but lets "diverged" be provable)
    divergence_detected      BOOLEAN NOT NULL DEFAULT FALSE,
    local_state_hash         TEXT DEFAULT NULL,
    remote_state_hash        TEXT DEFAULT NULL,

    -- attribution (multi-worker + debugging)
    config_hash       TEXT DEFAULT NULL,

    UNIQUE (vault_id, run_uuid)
    );

CREATE TABLE IF NOT EXISTS sync_conflicts
(
    id            SERIAL PRIMARY KEY,
    event_id      INTEGER NOT NULL REFERENCES sync_event (id) ON DELETE CASCADE,
    file_id       INTEGER NOT NULL REFERENCES files (fs_entry_id) ON DELETE CASCADE,

    conflict_type VARCHAR(12) NOT NULL
    CHECK (conflict_type IN ('mismatch', 'encryption', 'both')),

    resolved_at   TIMESTAMP DEFAULT NULL,

    resolution    VARCHAR(24) DEFAULT 'unresolved'
    CHECK (resolution IN ('unresolved', 'kept_local', 'kept_remote', 'kept_both', 'overwritten', 'fixed_remote_encryption')),

    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    UNIQUE (event_id, file_id)
    );

CREATE TABLE IF NOT EXISTS sync_conflict_reasons
(
    id              SERIAL PRIMARY KEY,
    conflict_id     INTEGER NOT NULL REFERENCES sync_conflicts(id) ON DELETE CASCADE,

    reason_code     VARCHAR(32) NOT NULL,
    reason_message  TEXT DEFAULT NULL,

    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    UNIQUE (conflict_id, reason_code)
    );

CREATE TABLE IF NOT EXISTS sync_conflict_artifacts (
                                                       id              SERIAL PRIMARY KEY,
                                                       conflict_id     INTEGER NOT NULL REFERENCES sync_conflicts(id) ON DELETE CASCADE,
    side            VARCHAR(12) NOT NULL
    CHECK (side IN ('local','upstream')),

    -- snapshot metadata for diff UI
    size_bytes      BIGINT,
    mime_type       VARCHAR(255),
    content_hash    VARCHAR(128),
    last_modified   TIMESTAMP,
    encryption_iv   TEXT,
    key_version     INTEGER,

    -- where to fetch bytes
    local_backing_path TEXT,     -- optional (for cached copy)

    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    UNIQUE (conflict_id, side)
    );


-- -----------------------------------
-- Throughput metrics (per-run buckets)
-- -----------------------------------
CREATE TABLE IF NOT EXISTS sync_throughput
(
    id               SERIAL PRIMARY KEY,

    vault_id          INTEGER NOT NULL,
    run_uuid          UUID NOT NULL,

    -- enforce association to a real sync_event run
    FOREIGN KEY (vault_id, run_uuid)
    REFERENCES sync_event (vault_id, run_uuid)
    ON DELETE CASCADE,

    metric_type      VARCHAR(12) NOT NULL
    CHECK (metric_type IN ('upload', 'download', 'rename', 'copy', 'delete')),

    num_ops          BIGINT NOT NULL DEFAULT 0,
    size_bytes       BIGINT NOT NULL DEFAULT 0,
    duration_ms      BIGINT NOT NULL DEFAULT 0,

    UNIQUE (vault_id, run_uuid, metric_type)
    );


-- ##################################
-- Indexes (dashboards + health queries)
-- ##################################

CREATE INDEX IF NOT EXISTS idx_sync_event_vault_begin
    ON sync_event (vault_id, timestamp_begin DESC);

CREATE INDEX IF NOT EXISTS idx_sync_event_vault_status_begin
    ON sync_event (vault_id, status, timestamp_begin DESC);

CREATE INDEX IF NOT EXISTS idx_sync_event_status_heartbeat
    ON sync_event (status, heartbeat_at);

-- common lookup for a specific run
CREATE INDEX IF NOT EXISTS idx_sync_event_vault_uuid
    ON sync_event (vault_id, run_uuid);

-- throughput lookups by run
CREATE INDEX IF NOT EXISTS idx_sync_throughput_run
    ON sync_throughput (vault_id, run_uuid);

-- conflicts by event
CREATE INDEX IF NOT EXISTS idx_sync_conflict_reason_conflict
    ON sync_conflict_reasons(conflict_id);

-- Helps retention delete and global cap selection
CREATE INDEX IF NOT EXISTS idx_sync_event_timestamp_begin ON sync_event (timestamp_begin);

-- Helps "keep newest N" by ordering; add DESC if your Postgres uses it for order-by optimization
CREATE INDEX IF NOT EXISTS idx_sync_event_timestamp_begin_desc ON sync_event (timestamp_begin DESC);


-- ##################################
-- Triggers
-- ##################################

DO $$ BEGIN
CREATE TRIGGER set_updated_at
    BEFORE UPDATE ON sync
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;


-- ###################################
-- Maintenance Function: Cleanup old sync events (batched, scalable)
-- ###################################

CREATE OR REPLACE FUNCTION vh.cleanup_sync_events(
    p_retention_days integer,
    p_max_entries integer,
    p_batch_size integer DEFAULT 5000,
    p_max_batches integer DEFAULT 200
)
RETURNS void
LANGUAGE plpgsql
AS $$
DECLARE
v_cutoff_ts     timestamptz;
    v_deleted       integer;
    v_batches       integer := 0;
    v_keep_cutoff_ts timestamptz;
BEGIN
    -- Guardrails (keep your stated minimums)
    IF p_retention_days IS NULL OR p_retention_days < 7 THEN
        p_retention_days := 7;
END IF;

    IF p_max_entries IS NULL OR p_max_entries < 1000 THEN
        p_max_entries := 1000;
END IF;

    IF p_batch_size IS NULL OR p_batch_size < 1000 THEN
        p_batch_size := 1000;
END IF;

    IF p_max_batches IS NULL OR p_max_batches < 1 THEN
        p_max_batches := 1;
END IF;

    v_cutoff_ts := now() - make_interval(days => p_retention_days);

    -- ------------------------------------------------------------
    -- 1) Time-based retention (batched)
    -- Use ctid batching to avoid long locks and huge transactions.
    -- ------------------------------------------------------------
    LOOP
EXIT WHEN v_batches >= p_max_batches;

WITH doomed AS (
    SELECT ctid
    FROM sync_event
    WHERE timestamp_begin < v_cutoff_ts
    ORDER BY timestamp_begin ASC
    LIMIT p_batch_size
    )
DELETE FROM sync_event se
    USING doomed
WHERE se.ctid = doomed.ctid;

GET DIAGNOSTICS v_deleted = ROW_COUNT;
v_batches := v_batches + 1;

        EXIT WHEN v_deleted = 0;
END LOOP;

    -- ------------------------------------------------------------
    -- 2) Cardinality cap (keep newest N globally) - batched
    -- Strategy:
    --   Find the timestamp_begin at the Nth newest row (the "keep cutoff").
    --   Delete anything strictly older than that cutoff in batches.
    --
    -- This avoids OFFSET-on-huge-table deletes each run, and avoids NOT IN.
    -- ------------------------------------------------------------

    -- If table has <= p_max_entries, nothing to do.
SELECT timestamp_begin
INTO v_keep_cutoff_ts
FROM sync_event
ORDER BY timestamp_begin DESC
OFFSET (p_max_entries - 1)
    LIMIT 1;

IF v_keep_cutoff_ts IS NULL THEN
        RETURN;
END IF;

    v_batches := 0;

    LOOP
EXIT WHEN v_batches >= p_max_batches;

WITH doomed AS (
    SELECT ctid
    FROM sync_event
    WHERE timestamp_begin < v_keep_cutoff_ts
    ORDER BY timestamp_begin ASC
    LIMIT p_batch_size
    )
DELETE FROM sync_event se
    USING doomed
WHERE se.ctid = doomed.ctid;

GET DIAGNOSTICS v_deleted = ROW_COUNT;
v_batches := v_batches + 1;

        EXIT WHEN v_deleted = 0;
END LOOP;

END;
$$;
