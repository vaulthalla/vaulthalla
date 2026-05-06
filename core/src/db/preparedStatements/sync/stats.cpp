#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSyncStats() const {
    conn_->prepare("sync_stats.config",
        R"SQL(
            SELECT
                s.id AS sync_id,
                s.enabled,
                s.interval,
                s.last_sync_at,
                s.last_success_at,
                rs.strategy AS configured_strategy,
                COALESCE(rs.conflict_policy, fs.conflict_policy) AS conflict_policy
            FROM sync s
            LEFT JOIN rsync rs ON rs.sync_id = s.id
            LEFT JOIN fsync fs ON fs.sync_id = s.id
            WHERE s.vault_id = $1
            LIMIT 1;
        )SQL"
    );

    conn_->prepare("sync_stats.latest_event",
        R"SQL(
            SELECT *
            FROM sync_event
            WHERE vault_id = $1
            ORDER BY timestamp_begin DESC
            LIMIT 1;
        )SQL"
    );

    conn_->prepare("sync_stats.last_error",
        R"SQL(
            SELECT
                timestamp_begin,
                stall_reason,
                error_code,
                error_message
            FROM sync_event
            WHERE vault_id = $1
              AND (
                    status = 'error'
                    OR error_code IS NOT NULL
                    OR error_message IS NOT NULL
                    OR stall_reason IS NOT NULL
                  )
            ORDER BY timestamp_begin DESC
            LIMIT 1;
        )SQL"
    );

    conn_->prepare("sync_stats.active_summary",
        R"SQL(
            SELECT
                COUNT(*) FILTER (WHERE status IN ('pending', 'running', 'stalled')) AS active_run_count,
                COUNT(*) FILTER (WHERE status = 'pending') AS pending_run_count,
                COUNT(*) FILTER (WHERE status = 'running') AS running_run_count,
                COUNT(*) FILTER (WHERE status = 'stalled') AS stalled_run_count,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(timestamp_begin) FILTER (
                    WHERE status IN ('pending', 'running', 'stalled')
                ))) AS oldest_active_age_seconds,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MAX(heartbeat_at) FILTER (
                    WHERE status IN ('pending', 'running', 'stalled') AND heartbeat_at IS NOT NULL
                ))) AS latest_heartbeat_age_seconds
            FROM sync_event
            WHERE vault_id = $1;
        )SQL"
    );

    conn_->prepare("sync_stats.event_windows",
        R"SQL(
            SELECT
                COUNT(*) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                      AND (status = 'error' OR error_code IS NOT NULL OR error_message IS NOT NULL)
                ) AS error_count_24h,
                COUNT(*) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                      AND (status = 'error' OR error_code IS NOT NULL OR error_message IS NOT NULL)
                ) AS error_count_7d,
                COALESCE(SUM(num_failed_ops) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ), 0) AS failed_ops_24h,
                COALESCE(SUM(num_failed_ops) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                ), 0) AS failed_ops_7d,
                COALESCE(SUM(retry_attempt) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ), 0) AS retry_count_24h,
                COALESCE(SUM(retry_attempt) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                ), 0) AS retry_count_7d,
                COALESCE(SUM(bytes_up) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ), 0) AS bytes_up_24h,
                COALESCE(SUM(bytes_down) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ), 0) AS bytes_down_24h,
                COALESCE(SUM(bytes_up) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                ), 0) AS bytes_up_7d,
                COALESCE(SUM(bytes_down) FILTER (
                    WHERE timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                ), 0) AS bytes_down_7d
            FROM sync_event
            WHERE vault_id = $1;
        )SQL"
    );

    conn_->prepare("sync_stats.conflict_windows",
        R"SQL(
            SELECT
                COUNT(*) FILTER (
                    WHERE c.resolution = 'unresolved' OR c.resolved_at IS NULL
                ) AS conflict_count_open,
                COUNT(*) FILTER (
                    WHERE c.created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS conflict_count_24h,
                COUNT(*) FILTER (
                    WHERE c.created_at >= CURRENT_TIMESTAMP - INTERVAL '7 days'
                ) AS conflict_count_7d
            FROM sync_conflicts c
            JOIN sync_event e ON e.id = c.event_id
            WHERE e.vault_id = $1;
        )SQL"
    );

    conn_->prepare("sync_stats.throughput_24h",
        R"SQL(
            SELECT
                COALESCE(
                    SUM(t.size_bytes)::double precision
                    / GREATEST(SUM(t.duration_ms)::double precision / 1000.0, 1.0),
                    0.0
                ) AS avg_throughput_bytes_per_sec_24h,
                COALESCE(
                    MAX(
                        t.size_bytes::double precision
                        / GREATEST(t.duration_ms::double precision / 1000.0, 1.0)
                    ),
                    0.0
                ) AS peak_throughput_bytes_per_sec_24h
            FROM sync_throughput t
            JOIN sync_event e ON e.vault_id = t.vault_id AND e.run_uuid = t.run_uuid
            WHERE t.vault_id = $1
              AND e.timestamp_begin >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
              AND t.duration_ms > 0;
        )SQL"
    );
}
