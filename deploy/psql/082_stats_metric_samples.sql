CREATE TABLE IF NOT EXISTS stats_threadpool_sample (
    id BIGSERIAL PRIMARY KEY,
    pool_name VARCHAR(96) NOT NULL,
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    window_seconds INTEGER NOT NULL CHECK (window_seconds > 0),
    sample_count INTEGER NOT NULL CHECK (sample_count >= 0),
    pressure_min DOUBLE PRECISION NOT NULL DEFAULT 0,
    pressure_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    pressure_max DOUBLE PRECISION NOT NULL DEFAULT 0,
    pressure_last DOUBLE PRECISION NOT NULL DEFAULT 0,
    queue_depth_min BIGINT NOT NULL DEFAULT 0 CHECK (queue_depth_min >= 0),
    queue_depth_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    queue_depth_max BIGINT NOT NULL DEFAULT 0 CHECK (queue_depth_max >= 0),
    queue_depth_last BIGINT NOT NULL DEFAULT 0 CHECK (queue_depth_last >= 0),
    busy_workers_min INTEGER NOT NULL DEFAULT 0 CHECK (busy_workers_min >= 0),
    busy_workers_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    busy_workers_max INTEGER NOT NULL DEFAULT 0 CHECK (busy_workers_max >= 0),
    busy_workers_last INTEGER NOT NULL DEFAULT 0 CHECK (busy_workers_last >= 0),
    idle_workers_min INTEGER NOT NULL DEFAULT 0 CHECK (idle_workers_min >= 0),
    idle_workers_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    idle_workers_max INTEGER NOT NULL DEFAULT 0 CHECK (idle_workers_max >= 0),
    idle_workers_last INTEGER NOT NULL DEFAULT 0 CHECK (idle_workers_last >= 0),
    borrowed_workers_min INTEGER NOT NULL DEFAULT 0 CHECK (borrowed_workers_min >= 0),
    borrowed_workers_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    borrowed_workers_max INTEGER NOT NULL DEFAULT 0 CHECK (borrowed_workers_max >= 0),
    borrowed_workers_last INTEGER NOT NULL DEFAULT 0 CHECK (borrowed_workers_last >= 0),
    pressured_sample_count INTEGER NOT NULL DEFAULT 0 CHECK (pressured_sample_count >= 0),
    saturated_sample_count INTEGER NOT NULL DEFAULT 0 CHECK (saturated_sample_count >= 0),
    queue_depth_high_water BIGINT NOT NULL DEFAULT 0 CHECK (queue_depth_high_water >= 0),
    pressure_high_water DOUBLE PRECISION NOT NULL DEFAULT 0,
    last_status VARCHAR(24) NOT NULL DEFAULT 'unknown',
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT stats_threadpool_sample_window_order CHECK (window_end > window_start),
    CONSTRAINT stats_threadpool_sample_pool_not_empty CHECK (length(trim(pool_name)) > 0),
    CONSTRAINT stats_threadpool_sample_status_known CHECK (last_status IN ('idle', 'normal', 'pressured', 'saturated', 'degraded', 'unknown'))
);

CREATE UNIQUE INDEX IF NOT EXISTS stats_threadpool_sample_pool_window_unique
    ON stats_threadpool_sample (pool_name, window_start);

CREATE INDEX IF NOT EXISTS idx_stats_threadpool_sample_pool_time
    ON stats_threadpool_sample (pool_name, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_threadpool_sample_time
    ON stats_threadpool_sample (window_end DESC);

CREATE TABLE IF NOT EXISTS stats_fuse_op_sample (
    id BIGSERIAL PRIMARY KEY,
    op VARCHAR(48) NOT NULL,
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    window_seconds INTEGER NOT NULL CHECK (window_seconds > 0),
    count_delta BIGINT NOT NULL DEFAULT 0 CHECK (count_delta >= 0),
    success_delta BIGINT NOT NULL DEFAULT 0 CHECK (success_delta >= 0),
    error_delta BIGINT NOT NULL DEFAULT 0 CHECK (error_delta >= 0),
    expected_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (expected_error_delta >= 0),
    alertable_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (alertable_error_delta >= 0),
    error_rate DOUBLE PRECISION,
    expected_error_rate DOUBLE PRECISION,
    alertable_error_rate DOUBLE PRECISION,
    read_bytes_delta BIGINT NOT NULL DEFAULT 0 CHECK (read_bytes_delta >= 0),
    write_bytes_delta BIGINT NOT NULL DEFAULT 0 CHECK (write_bytes_delta >= 0),
    avg_latency_ms DOUBLE PRECISION,
    max_latency_ms DOUBLE PRECISION,
    counter_reset BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT stats_fuse_op_sample_window_order CHECK (window_end > window_start),
    CONSTRAINT stats_fuse_op_sample_op_not_empty CHECK (length(trim(op)) > 0)
);

DO $$
DECLARE
    had_alertable_fuse_errors BOOLEAN;
BEGIN
    SELECT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'stats_fuse_op_sample'
          AND column_name = 'alertable_error_delta'
    ) INTO had_alertable_fuse_errors;

    ALTER TABLE stats_fuse_op_sample
        ADD COLUMN IF NOT EXISTS expected_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (expected_error_delta >= 0),
        ADD COLUMN IF NOT EXISTS alertable_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (alertable_error_delta >= 0),
        ADD COLUMN IF NOT EXISTS expected_error_rate DOUBLE PRECISION,
        ADD COLUMN IF NOT EXISTS alertable_error_rate DOUBLE PRECISION;

    IF NOT had_alertable_fuse_errors THEN
        UPDATE stats_fuse_op_sample
        SET alertable_error_delta = error_delta,
            alertable_error_rate = error_rate
        WHERE error_delta > 0 OR error_rate IS NOT NULL;
    END IF;
END $$;

CREATE UNIQUE INDEX IF NOT EXISTS stats_fuse_op_sample_op_window_unique
    ON stats_fuse_op_sample (op, window_start);

CREATE INDEX IF NOT EXISTS idx_stats_fuse_op_sample_op_time
    ON stats_fuse_op_sample (op, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_fuse_op_sample_time
    ON stats_fuse_op_sample (window_end DESC);

CREATE TABLE IF NOT EXISTS stats_cache_sample (
    id BIGSERIAL PRIMARY KEY,
    source VARCHAR(16) NOT NULL CHECK (source IN ('fs', 'http')),
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    window_seconds INTEGER NOT NULL CHECK (window_seconds > 0),
    sample_count INTEGER NOT NULL CHECK (sample_count >= 0),
    hit_delta BIGINT NOT NULL DEFAULT 0 CHECK (hit_delta >= 0),
    miss_delta BIGINT NOT NULL DEFAULT 0 CHECK (miss_delta >= 0),
    eviction_delta BIGINT NOT NULL DEFAULT 0 CHECK (eviction_delta >= 0),
    insert_delta BIGINT NOT NULL DEFAULT 0 CHECK (insert_delta >= 0),
    invalidation_delta BIGINT NOT NULL DEFAULT 0 CHECK (invalidation_delta >= 0),
    hit_rate DOUBLE PRECISION,
    bytes_read_delta BIGINT NOT NULL DEFAULT 0 CHECK (bytes_read_delta >= 0),
    bytes_written_delta BIGINT NOT NULL DEFAULT 0 CHECK (bytes_written_delta >= 0),
    occupancy_min DOUBLE PRECISION NOT NULL DEFAULT 0,
    occupancy_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    occupancy_max DOUBLE PRECISION NOT NULL DEFAULT 0,
    occupancy_last DOUBLE PRECISION NOT NULL DEFAULT 0,
    used_bytes_min BIGINT NOT NULL DEFAULT 0 CHECK (used_bytes_min >= 0),
    used_bytes_avg DOUBLE PRECISION NOT NULL DEFAULT 0,
    used_bytes_max BIGINT NOT NULL DEFAULT 0 CHECK (used_bytes_max >= 0),
    used_bytes_last BIGINT NOT NULL DEFAULT 0 CHECK (used_bytes_last >= 0),
    op_count_delta BIGINT NOT NULL DEFAULT 0 CHECK (op_count_delta >= 0),
    avg_latency_ms DOUBLE PRECISION,
    max_latency_ms DOUBLE PRECISION,
    counter_reset BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT stats_cache_sample_window_order CHECK (window_end > window_start)
);

CREATE UNIQUE INDEX IF NOT EXISTS stats_cache_sample_source_window_unique
    ON stats_cache_sample (source, window_start);

CREATE INDEX IF NOT EXISTS idx_stats_cache_sample_source_time
    ON stats_cache_sample (source, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_cache_sample_time
    ON stats_cache_sample (window_end DESC);

CREATE TABLE IF NOT EXISTS stats_metric_sample (
    id BIGSERIAL PRIMARY KEY,
    scope VARCHAR(24) NOT NULL CHECK (scope IN ('system', 'vault')),
    vault_id INTEGER REFERENCES vault(id) ON DELETE CASCADE,
    metric_key VARCHAR(96) NOT NULL,
    series_key VARCHAR(128) NOT NULL DEFAULT '',
    series_label VARCHAR(160) NOT NULL,
    unit VARCHAR(32) NOT NULL DEFAULT 'unknown',
    snapshot_type VARCHAR(64) NOT NULL,
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    window_seconds INTEGER NOT NULL CHECK (window_seconds > 0),
    sample_count INTEGER NOT NULL CHECK (sample_count >= 0),
    value_min DOUBLE PRECISION,
    value_avg DOUBLE PRECISION,
    value_max DOUBLE PRECISION,
    value_last DOUBLE PRECISION,
    delta_value DOUBLE PRECISION,
    rate_per_second DOUBLE PRECISION,
    tags JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (
        (scope = 'system' AND vault_id IS NULL)
        OR
        (scope = 'vault' AND vault_id IS NOT NULL)
    ),
    CONSTRAINT stats_metric_sample_window_order CHECK (window_end > window_start),
    CONSTRAINT stats_metric_sample_metric_not_empty CHECK (length(trim(metric_key)) > 0),
    CONSTRAINT stats_metric_sample_series_label_not_empty CHECK (length(trim(series_label)) > 0),
    CONSTRAINT stats_metric_sample_tags_object CHECK (jsonb_typeof(tags) = 'object')
);

CREATE INDEX IF NOT EXISTS idx_stats_metric_sample_metric_time
    ON stats_metric_sample (scope, metric_key, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_metric_sample_series_time
    ON stats_metric_sample (scope, metric_key, series_key, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_metric_sample_vault_metric_time
    ON stats_metric_sample (vault_id, metric_key, window_end DESC)
    WHERE vault_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_stats_metric_sample_time
    ON stats_metric_sample (window_end DESC);

CREATE TABLE IF NOT EXISTS stats_metric_rollup (
    id BIGSERIAL PRIMARY KEY,
    scope VARCHAR(24) NOT NULL CHECK (scope IN ('system', 'vault')),
    vault_id INTEGER REFERENCES vault(id) ON DELETE CASCADE,
    metric_key VARCHAR(96) NOT NULL,
    series_key VARCHAR(128) NOT NULL DEFAULT '',
    series_label VARCHAR(160) NOT NULL,
    unit VARCHAR(32) NOT NULL DEFAULT 'unknown',
    snapshot_type VARCHAR(64) NOT NULL,
    resolution_seconds INTEGER NOT NULL CHECK (resolution_seconds IN (300, 3600)),
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    value_min DOUBLE PRECISION,
    value_avg DOUBLE PRECISION,
    value_max DOUBLE PRECISION,
    value_last DOUBLE PRECISION,
    delta_value DOUBLE PRECISION,
    rate_per_second DOUBLE PRECISION,
    source_sample_count INTEGER NOT NULL CHECK (source_sample_count >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (
        (scope = 'system' AND vault_id IS NULL)
        OR
        (scope = 'vault' AND vault_id IS NOT NULL)
    ),
    CONSTRAINT stats_metric_rollup_window_order CHECK (window_end > window_start),
    CONSTRAINT stats_metric_rollup_metric_not_empty CHECK (length(trim(metric_key)) > 0),
    CONSTRAINT stats_metric_rollup_series_label_not_empty CHECK (length(trim(series_label)) > 0)
);

CREATE UNIQUE INDEX IF NOT EXISTS stats_metric_rollup_system_unique
    ON stats_metric_rollup (resolution_seconds, metric_key, series_key, snapshot_type, window_start)
    WHERE scope = 'system' AND vault_id IS NULL;

CREATE UNIQUE INDEX IF NOT EXISTS stats_metric_rollup_vault_unique
    ON stats_metric_rollup (resolution_seconds, vault_id, metric_key, series_key, snapshot_type, window_start)
    WHERE scope = 'vault' AND vault_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_stats_metric_rollup_metric_time
    ON stats_metric_rollup (scope, resolution_seconds, metric_key, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_metric_rollup_series_time
    ON stats_metric_rollup (scope, resolution_seconds, metric_key, series_key, window_end DESC);

CREATE INDEX IF NOT EXISTS idx_stats_metric_rollup_vault_metric_time
    ON stats_metric_rollup (vault_id, resolution_seconds, metric_key, window_end DESC)
    WHERE vault_id IS NOT NULL;

DO $$ BEGIN
CREATE TRIGGER set_stats_metric_rollup_updated_at
    BEFORE UPDATE ON stats_metric_rollup
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
