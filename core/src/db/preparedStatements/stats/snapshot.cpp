#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedStatsSnapshots() const {
    conn_->prepare("stats_snapshot.insert_system",
        R"SQL(
            INSERT INTO stats_snapshot (scope, vault_id, snapshot_type, payload)
            VALUES ('system', NULL, $1, $2::jsonb);
        )SQL"
    );

    conn_->prepare("stats_snapshot.insert_vault",
        R"SQL(
            INSERT INTO stats_snapshot (scope, vault_id, snapshot_type, payload)
            VALUES ('vault', $1, $2, $3::jsonb);
        )SQL"
    );

    conn_->prepare("stats_snapshot.purge_older_than",
        R"SQL(
            DELETE FROM stats_snapshot
            WHERE created_at < CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 day');
        )SQL"
    );

    conn_->prepare("stats_snapshot.system_trends",
        R"SQL(
            WITH samples AS (
                SELECT
                    'db_size_bytes' AS metric_key,
                    'Database size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'db_size_bytes', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'db_cache_hit_ratio' AS metric_key,
                    'DB cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'cache_hit_ratio', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'db_connections_total' AS metric_key,
                    'DB connections' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'connections_total', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'fuse_total_ops' AS metric_key,
                    'FUSE operations' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'total_ops', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.fuse'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'fuse_error_rate' AS metric_key,
                    'FUSE error rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'error_rate', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.fuse'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'threadpool_queue_depth' AS metric_key,
                    'Thread pool queue' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'total_queue_depth', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.threadpools'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'threadpool_pressure' AS metric_key,
                    'Thread pool pressure' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'max_pressure_ratio', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.threadpools'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'threadpool_pool_pressure:' || COALESCE(pool->>'name', 'unknown') AS metric_key,
                    COALESCE(pool->>'name', 'Pool') || ' pressure' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(pool->>'pressure_ratio', '')::double precision AS value
                FROM stats_snapshot
                CROSS JOIN LATERAL jsonb_array_elements(COALESCE(payload->'pools', '[]'::jsonb)) AS pool
                WHERE scope = 'system'
                  AND snapshot_type = 'system.threadpools'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'fs_cache_hit_rate' AS metric_key,
                    'FS cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    CASE
                        WHEN COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                           + COALESCE(NULLIF(payload->'fs'->>'misses', '')::double precision, 0) > 0
                        THEN COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                           / (
                                COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                                + COALESCE(NULLIF(payload->'fs'->>'misses', '')::double precision, 0)
                             )
                        ELSE NULL
                    END AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.cache'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'http_cache_hit_rate' AS metric_key,
                    'HTTP cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    CASE
                        WHEN COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                           + COALESCE(NULLIF(payload->'http'->>'misses', '')::double precision, 0) > 0
                        THEN COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                           / (
                                COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                                + COALESCE(NULLIF(payload->'http'->>'misses', '')::double precision, 0)
                             )
                        ELSE NULL
                    END AS value
                FROM stats_snapshot
                WHERE scope = 'system'
                  AND snapshot_type = 'system.cache'
                  AND created_at >= CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour')
            )
            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM samples
            WHERE value IS NOT NULL
            ORDER BY metric_key, created_at;
        )SQL"
    );

    conn_->prepare("stats_snapshot.vault_trends",
        R"SQL(
            WITH samples AS (
                SELECT
                    'capacity_logical_size' AS metric_key,
                    'Logical size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'logical_size', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'capacity_physical_size' AS metric_key,
                    'Physical size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'physical_size', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'capacity_file_count' AS metric_key,
                    'Files' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'file_count', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'sync_errors_24h' AS metric_key,
                    'Sync errors 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'error_count_24h', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'sync_failed_ops_24h' AS metric_key,
                    'Failed sync ops 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'failed_ops_24h', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'sync_bytes_24h' AS metric_key,
                    'Sync bytes 24h' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'bytes_total_24h', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'activity_mutations_24h' AS metric_key,
                    'Mutations 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    COALESCE(NULLIF(payload->>'uploads_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'deletes_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'renames_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'moves_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'copies_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'restores_24h', '')::double precision, 0) AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.activity'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')

                UNION ALL

                SELECT
                    'activity_bytes_added_24h' AS metric_key,
                    'Bytes added 24h' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'bytes_added_24h', '')::double precision AS value
                FROM stats_snapshot
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.activity'
                  AND created_at >= CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour')
            )
            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM samples
            WHERE value IS NOT NULL
            ORDER BY metric_key, created_at;
        )SQL"
    );

    conn_->prepare("stats_snapshot.system_trends_typed",
        R"SQL(
            WITH params AS (
                SELECT
                    $1::int AS window_hours,
                    CURRENT_TIMESTAMP - ($1::int * INTERVAL '1 hour') AS cutoff,
                    CASE WHEN $1::int <= 168 THEN 300 ELSE 3600 END AS rollup_resolution
            ),
            raw_typed AS (
                SELECT
                    CASE
                        WHEN m.series_key <> '' THEN m.metric_key || ':' || m.series_key
                        ELSE m.metric_key
                    END AS metric_key,
                    m.series_label AS label,
                    m.unit,
                    m.snapshot_type,
                    m.window_end AS created_at,
                    COALESCE(m.value_last, m.value_avg, m.value_max, m.value_min, m.rate_per_second, m.delta_value) AS value
                FROM stats_metric_sample m
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND m.scope = 'system'
                  AND m.vault_id IS NULL
                  AND m.window_end >= p.cutoff

                UNION ALL

                SELECT
                    'threadpool_pressure' AS metric_key,
                    'Aggregate pressure' AS label,
                    'ratio' AS unit,
                    'system.threadpools' AS snapshot_type,
                    s.window_end AS created_at,
                    s.pressure_avg AS value
                FROM stats_threadpool_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.pool_name = '__aggregate__'
                  AND s.window_end >= p.cutoff

                UNION ALL

                SELECT
                    'threadpool_pool_pressure:' || s.pool_name AS metric_key,
                    s.pool_name || ' pressure' AS label,
                    'ratio' AS unit,
                    'system.threadpools' AS snapshot_type,
                    s.window_end AS created_at,
                    s.pressure_avg AS value
                FROM stats_threadpool_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.pool_name <> '__aggregate__'
                  AND s.window_end >= p.cutoff

                UNION ALL

                SELECT
                    'threadpool_queue_depth' AS metric_key,
                    'Thread pool queue' AS label,
                    'count' AS unit,
                    'system.threadpools' AS snapshot_type,
                    s.window_end AS created_at,
                    s.queue_depth_avg AS value
                FROM stats_threadpool_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.pool_name = '__aggregate__'
                  AND s.window_end >= p.cutoff

                UNION ALL

                SELECT
                    'fuse_ops_per_second' AS metric_key,
                    'FUSE ops/sec' AS label,
                    'ops/s' AS unit,
                    'system.fuse' AS snapshot_type,
                    s.window_end AS created_at,
                    SUM(s.count_delta)::double precision / NULLIF(MAX(s.window_seconds), 0) AS value
                FROM stats_fuse_op_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
                  AND NOT s.counter_reset
                GROUP BY s.window_end

                UNION ALL

                SELECT
                    'fuse_total_ops' AS metric_key,
                    'FUSE operations' AS label,
                    'count' AS unit,
                    'system.fuse' AS snapshot_type,
                    s.window_end AS created_at,
                    SUM(s.count_delta)::double precision AS value
                FROM stats_fuse_op_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
                  AND NOT s.counter_reset
                GROUP BY s.window_end

                UNION ALL

                SELECT
                    'fuse_error_rate' AS metric_key,
                    'FUSE error rate' AS label,
                    'ratio' AS unit,
                    'system.fuse' AS snapshot_type,
                    s.window_end AS created_at,
                    SUM(s.error_delta)::double precision / NULLIF(SUM(s.count_delta), 0) AS value
                FROM stats_fuse_op_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
                  AND NOT s.counter_reset
                GROUP BY s.window_end

                UNION ALL

                SELECT
                    'fuse_latency_avg_ms' AS metric_key,
                    'FUSE latency' AS label,
                    'ms' AS unit,
                    'system.fuse' AS snapshot_type,
                    s.window_end AS created_at,
                    SUM(s.avg_latency_ms * s.count_delta)::double precision / NULLIF(SUM(s.count_delta), 0) AS value
                FROM stats_fuse_op_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
                  AND NOT s.counter_reset
                  AND s.avg_latency_ms IS NOT NULL
                GROUP BY s.window_end

                UNION ALL

                SELECT
                    CASE WHEN s.source = 'http' THEN 'http_cache_hit_rate' ELSE 'fs_cache_hit_rate' END AS metric_key,
                    CASE WHEN s.source = 'http' THEN 'HTTP cache hit rate' ELSE 'FS cache hit rate' END AS label,
                    'ratio' AS unit,
                    'system.cache' AS snapshot_type,
                    s.window_end AS created_at,
                    s.hit_rate AS value
                FROM stats_cache_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
                  AND s.hit_rate IS NOT NULL

                UNION ALL

                SELECT
                    CASE WHEN s.source = 'http' THEN 'http_cache_occupancy' ELSE 'fs_cache_occupancy' END AS metric_key,
                    CASE WHEN s.source = 'http' THEN 'HTTP cache occupancy' ELSE 'FS cache occupancy' END AS label,
                    'ratio' AS unit,
                    'system.cache' AS snapshot_type,
                    s.window_end AS created_at,
                    s.occupancy_avg AS value
                FROM stats_cache_sample s
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND s.window_end >= p.cutoff
            ),
            rollup_typed AS (
                SELECT
                    CASE
                        WHEN r.series_key <> '' THEN r.metric_key || ':' || r.series_key
                        ELSE r.metric_key
                    END AS metric_key,
                    r.series_label AS label,
                    r.unit,
                    r.snapshot_type,
                    r.window_end AS created_at,
                    COALESCE(r.value_last, r.value_avg, r.value_max, r.value_min, r.rate_per_second, r.delta_value) AS value
                FROM stats_metric_rollup r
                CROSS JOIN params p
                WHERE p.window_hours > 24
                  AND r.scope = 'system'
                  AND r.vault_id IS NULL
                  AND r.resolution_seconds = p.rollup_resolution
                  AND r.window_end >= p.cutoff
            ),
            typed AS (
                SELECT * FROM raw_typed
                UNION ALL
                SELECT * FROM rollup_typed
            ),
            fallback AS (
                SELECT
                    'db_size_bytes' AS metric_key,
                    'Database size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'db_size_bytes', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'db_cache_hit_ratio' AS metric_key,
                    'DB cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'cache_hit_ratio', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'db_connections_total' AS metric_key,
                    'DB connections' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'connections_total', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.db'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'fuse_total_ops' AS metric_key,
                    'FUSE operations' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'total_ops', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.fuse'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'fuse_error_rate' AS metric_key,
                    'FUSE error rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'error_rate', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.fuse'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'threadpool_pressure' AS metric_key,
                    'Thread pool pressure' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'max_pressure_ratio', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.threadpools'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'threadpool_pool_pressure:' || COALESCE(pool->>'name', 'unknown') AS metric_key,
                    COALESCE(pool->>'name', 'Pool') || ' pressure' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(pool->>'pressure_ratio', '')::double precision AS value
                FROM stats_snapshot
                CROSS JOIN params
                CROSS JOIN LATERAL jsonb_array_elements(COALESCE(payload->'pools', '[]'::jsonb)) AS pool
                WHERE scope = 'system'
                  AND snapshot_type = 'system.threadpools'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'fs_cache_hit_rate' AS metric_key,
                    'FS cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    CASE
                        WHEN COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                           + COALESCE(NULLIF(payload->'fs'->>'misses', '')::double precision, 0) > 0
                        THEN COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                           / (
                                COALESCE(NULLIF(payload->'fs'->>'hits', '')::double precision, 0)
                                + COALESCE(NULLIF(payload->'fs'->>'misses', '')::double precision, 0)
                             )
                        ELSE NULL
                    END AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.cache'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'http_cache_hit_rate' AS metric_key,
                    'HTTP cache hit rate' AS label,
                    'ratio' AS unit,
                    snapshot_type,
                    created_at,
                    CASE
                        WHEN COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                           + COALESCE(NULLIF(payload->'http'->>'misses', '')::double precision, 0) > 0
                        THEN COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                           / (
                                COALESCE(NULLIF(payload->'http'->>'hits', '')::double precision, 0)
                                + COALESCE(NULLIF(payload->'http'->>'misses', '')::double precision, 0)
                             )
                        ELSE NULL
                    END AS value
                FROM stats_snapshot, params
                WHERE scope = 'system'
                  AND snapshot_type = 'system.cache'
                  AND created_at >= cutoff
            )
            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM typed
            WHERE value IS NOT NULL
              AND EXISTS (SELECT 1 FROM typed WHERE value IS NOT NULL)

            UNION ALL

            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM fallback
            WHERE value IS NOT NULL
              AND NOT EXISTS (SELECT 1 FROM typed WHERE value IS NOT NULL)
            ORDER BY metric_key, created_at;
        )SQL"
    );

    conn_->prepare("stats_snapshot.vault_trends_typed",
        R"SQL(
            WITH params AS (
                SELECT
                    $2::int AS window_hours,
                    CURRENT_TIMESTAMP - ($2::int * INTERVAL '1 hour') AS cutoff,
                    CASE WHEN $2::int <= 168 THEN 300 ELSE 3600 END AS rollup_resolution
            ),
            raw_typed AS (
                SELECT
                    CASE
                        WHEN m.series_key <> '' THEN m.metric_key || ':' || m.series_key
                        ELSE m.metric_key
                    END AS metric_key,
                    m.series_label AS label,
                    m.unit,
                    m.snapshot_type,
                    m.window_end AS created_at,
                    COALESCE(m.value_last, m.value_avg, m.value_max, m.value_min, m.rate_per_second, m.delta_value) AS value
                FROM stats_metric_sample m
                CROSS JOIN params p
                WHERE p.window_hours <= 24
                  AND m.scope = 'vault'
                  AND m.vault_id = $1
                  AND m.window_end >= p.cutoff
            ),
            rollup_typed AS (
                SELECT
                    CASE
                        WHEN r.series_key <> '' THEN r.metric_key || ':' || r.series_key
                        ELSE r.metric_key
                    END AS metric_key,
                    r.series_label AS label,
                    r.unit,
                    r.snapshot_type,
                    r.window_end AS created_at,
                    COALESCE(r.value_last, r.value_avg, r.value_max, r.value_min, r.rate_per_second, r.delta_value) AS value
                FROM stats_metric_rollup r
                CROSS JOIN params p
                WHERE p.window_hours > 24
                  AND r.scope = 'vault'
                  AND r.vault_id = $1
                  AND r.resolution_seconds = p.rollup_resolution
                  AND r.window_end >= p.cutoff
            ),
            typed AS (
                SELECT * FROM raw_typed
                UNION ALL
                SELECT * FROM rollup_typed
            ),
            fallback AS (
                SELECT
                    'capacity_logical_size' AS metric_key,
                    'Logical size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'logical_size', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'capacity_physical_size' AS metric_key,
                    'Physical size' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'physical_size', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'capacity_file_count' AS metric_key,
                    'Files' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'file_count', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.capacity'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'sync_errors_24h' AS metric_key,
                    'Sync errors 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'error_count_24h', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'sync_failed_ops_24h' AS metric_key,
                    'Failed sync ops 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'failed_ops_24h', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'sync_bytes_24h' AS metric_key,
                    'Sync bytes 24h' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'bytes_total_24h', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.sync'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'activity_mutations_24h' AS metric_key,
                    'Mutations 24h' AS label,
                    'count' AS unit,
                    snapshot_type,
                    created_at,
                    COALESCE(NULLIF(payload->>'uploads_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'deletes_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'renames_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'moves_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'copies_24h', '')::double precision, 0)
                    + COALESCE(NULLIF(payload->>'restores_24h', '')::double precision, 0) AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.activity'
                  AND created_at >= cutoff

                UNION ALL

                SELECT
                    'activity_bytes_added_24h' AS metric_key,
                    'Bytes added 24h' AS label,
                    'bytes' AS unit,
                    snapshot_type,
                    created_at,
                    NULLIF(payload->>'bytes_added_24h', '')::double precision AS value
                FROM stats_snapshot, params
                WHERE scope = 'vault'
                  AND vault_id = $1
                  AND snapshot_type = 'vault.activity'
                  AND created_at >= cutoff
            )
            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM typed
            WHERE value IS NOT NULL
              AND EXISTS (SELECT 1 FROM typed WHERE value IS NOT NULL)

            UNION ALL

            SELECT
                metric_key,
                label,
                unit,
                snapshot_type,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                value
            FROM fallback
            WHERE value IS NOT NULL
              AND NOT EXISTS (SELECT 1 FROM typed WHERE value IS NOT NULL)
            ORDER BY metric_key, created_at;
        )SQL"
    );
}
