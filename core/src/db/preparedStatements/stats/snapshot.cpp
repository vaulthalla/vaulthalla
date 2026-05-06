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
}
