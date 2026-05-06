#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedOperationStats() const {
    conn_->prepare("operation_stats.system_operations",
        R"SQL(
            SELECT
                COUNT(*) FILTER (WHERE status = 'pending') AS pending_operations,
                COUNT(*) FILTER (WHERE status = 'in_progress') AS in_progress_operations,
                COUNT(*) FILTER (
                    WHERE status = 'error'
                      AND COALESCE(completed_at, created_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS failed_operations_24h,
                COUNT(*) FILTER (
                    WHERE status = 'cancelled'
                      AND COALESCE(completed_at, created_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS cancelled_operations_24h,
                COUNT(*) FILTER (
                    WHERE status IN ('pending', 'in_progress')
                      AND created_at < CURRENT_TIMESTAMP - INTERVAL '15 minutes'
                ) AS stalled_operations,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(created_at) FILTER (WHERE status = 'pending'))) AS oldest_pending_operation_age_seconds,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(created_at) FILTER (WHERE status = 'in_progress'))) AS oldest_in_progress_operation_age_seconds,
                COUNT(*) FILTER (WHERE operation = 'move') AS move_count,
                COUNT(*) FILTER (WHERE operation = 'copy') AS copy_count,
                COUNT(*) FILTER (WHERE operation = 'rename') AS rename_count,
                COUNT(*) FILTER (WHERE status = 'pending') AS status_pending_count,
                COUNT(*) FILTER (WHERE status = 'in_progress') AS status_in_progress_count,
                COUNT(*) FILTER (WHERE status = 'success') AS status_success_count,
                COUNT(*) FILTER (WHERE status = 'error') AS status_error_count,
                COUNT(*) FILTER (WHERE status = 'cancelled') AS status_cancelled_count
            FROM operations;
        )SQL"
    );

    conn_->prepare("operation_stats.vault_operations",
        R"SQL(
            SELECT
                COUNT(*) FILTER (WHERE o.status = 'pending') AS pending_operations,
                COUNT(*) FILTER (WHERE o.status = 'in_progress') AS in_progress_operations,
                COUNT(*) FILTER (
                    WHERE o.status = 'error'
                      AND COALESCE(o.completed_at, o.created_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS failed_operations_24h,
                COUNT(*) FILTER (
                    WHERE o.status = 'cancelled'
                      AND COALESCE(o.completed_at, o.created_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS cancelled_operations_24h,
                COUNT(*) FILTER (
                    WHERE o.status IN ('pending', 'in_progress')
                      AND o.created_at < CURRENT_TIMESTAMP - INTERVAL '15 minutes'
                ) AS stalled_operations,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(o.created_at) FILTER (WHERE o.status = 'pending'))) AS oldest_pending_operation_age_seconds,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(o.created_at) FILTER (WHERE o.status = 'in_progress'))) AS oldest_in_progress_operation_age_seconds,
                COUNT(*) FILTER (WHERE o.operation = 'move') AS move_count,
                COUNT(*) FILTER (WHERE o.operation = 'copy') AS copy_count,
                COUNT(*) FILTER (WHERE o.operation = 'rename') AS rename_count,
                COUNT(*) FILTER (WHERE o.status = 'pending') AS status_pending_count,
                COUNT(*) FILTER (WHERE o.status = 'in_progress') AS status_in_progress_count,
                COUNT(*) FILTER (WHERE o.status = 'success') AS status_success_count,
                COUNT(*) FILTER (WHERE o.status = 'error') AS status_error_count,
                COUNT(*) FILTER (WHERE o.status = 'cancelled') AS status_cancelled_count
            FROM operations o
            JOIN fs_entry fs ON fs.id = o.fs_entry_id
            WHERE fs.vault_id = $1;
        )SQL"
    );

    conn_->prepare("operation_stats.system_uploads",
        R"SQL(
            SELECT
                COUNT(*) FILTER (WHERE status IN ('pending', 'receiving')) AS active_share_uploads,
                COUNT(*) FILTER (
                    WHERE status IN ('pending', 'receiving')
                      AND started_at < CURRENT_TIMESTAMP - INTERVAL '15 minutes'
                ) AS stalled_share_uploads,
                COUNT(*) FILTER (
                    WHERE status = 'failed'
                      AND COALESCE(completed_at, started_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS failed_share_uploads_24h,
                COALESCE(SUM(expected_size_bytes) FILTER (WHERE status IN ('pending', 'receiving')), 0) AS upload_bytes_expected_active,
                COALESCE(SUM(received_size_bytes) FILTER (WHERE status IN ('pending', 'receiving')), 0) AS upload_bytes_received_active,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(started_at) FILTER (WHERE status IN ('pending', 'receiving')))) AS oldest_active_upload_age_seconds
            FROM share_upload;
        )SQL"
    );

    conn_->prepare("operation_stats.vault_uploads",
        R"SQL(
            SELECT
                COUNT(*) FILTER (WHERE su.status IN ('pending', 'receiving')) AS active_share_uploads,
                COUNT(*) FILTER (
                    WHERE su.status IN ('pending', 'receiving')
                      AND su.started_at < CURRENT_TIMESTAMP - INTERVAL '15 minutes'
                ) AS stalled_share_uploads,
                COUNT(*) FILTER (
                    WHERE su.status = 'failed'
                      AND COALESCE(su.completed_at, su.started_at) >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS failed_share_uploads_24h,
                COALESCE(SUM(su.expected_size_bytes) FILTER (WHERE su.status IN ('pending', 'receiving')), 0) AS upload_bytes_expected_active,
                COALESCE(SUM(su.received_size_bytes) FILTER (WHERE su.status IN ('pending', 'receiving')), 0) AS upload_bytes_received_active,
                EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(su.started_at) FILTER (WHERE su.status IN ('pending', 'receiving')))) AS oldest_active_upload_age_seconds
            FROM share_upload su
            JOIN share_link sl ON sl.id = su.share_id
            WHERE sl.vault_id = $1;
        )SQL"
    );

    conn_->prepare("operation_stats.system_recent_errors",
        R"SQL(
            SELECT *
            FROM (
                SELECT
                    'operation' AS source,
                    operation,
                    status,
                    target,
                    destination_path AS path,
                    error,
                    COALESCE(completed_at, created_at)::timestamptz AS occurred_at
                FROM operations
                WHERE status IN ('error', 'cancelled') OR error IS NOT NULL
                UNION ALL
                SELECT
                    'share_upload' AS source,
                    'upload' AS operation,
                    status,
                    'file' AS target,
                    target_path AS path,
                    error,
                    COALESCE(completed_at, started_at) AS occurred_at
                FROM share_upload
                WHERE status IN ('failed', 'cancelled') OR error IS NOT NULL
            ) recent
            ORDER BY occurred_at DESC
            LIMIT 8;
        )SQL"
    );

    conn_->prepare("operation_stats.vault_recent_errors",
        R"SQL(
            SELECT *
            FROM (
                SELECT
                    'operation' AS source,
                    o.operation,
                    o.status,
                    o.target,
                    o.destination_path AS path,
                    o.error,
                    COALESCE(o.completed_at, o.created_at)::timestamptz AS occurred_at
                FROM operations o
                JOIN fs_entry fs ON fs.id = o.fs_entry_id
                WHERE fs.vault_id = $1
                  AND (o.status IN ('error', 'cancelled') OR o.error IS NOT NULL)
                UNION ALL
                SELECT
                    'share_upload' AS source,
                    'upload' AS operation,
                    su.status,
                    'file' AS target,
                    su.target_path AS path,
                    su.error,
                    COALESCE(su.completed_at, su.started_at) AS occurred_at
                FROM share_upload su
                JOIN share_link sl ON sl.id = su.share_id
                WHERE sl.vault_id = $1
                  AND (su.status IN ('failed', 'cancelled') OR su.error IS NOT NULL)
            ) recent
            ORDER BY occurred_at DESC
            LIMIT 8;
        )SQL"
    );
}
