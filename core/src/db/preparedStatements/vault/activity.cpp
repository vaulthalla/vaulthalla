#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedVaultActivity() const {
    conn_->prepare("vault_activity.summary",
        R"SQL(
            WITH activity_events AS (
                SELECT
                    'file_activity'::text AS source,
                    CASE
                        WHEN fa.action IN ('created', 'modified') THEN 'upload'
                        ELSE fa.action
                    END AS action,
                    fs.path AS path,
                    fa.user_id,
                    COALESCE(f.size_bytes, 0)::bigint AS bytes,
                    fa.timestamp AS occurred_at,
                    NULL::text AS status,
                    NULL::text AS error
                FROM file_activity fa
                JOIN files f ON f.fs_entry_id = fa.file_id
                JOIN fs_entry fs ON fs.id = f.fs_entry_id
                WHERE fs.vault_id = $1

                UNION ALL

                SELECT
                    'trash'::text AS source,
                    'delete'::text AS action,
                    ft.path,
                    ft.trashed_by AS user_id,
                    COALESCE(ft.size_bytes, 0)::bigint AS bytes,
                    ft.trashed_at AS occurred_at,
                    'success'::text AS status,
                    NULL::text AS error
                FROM files_trashed ft
                WHERE ft.vault_id = $1

                UNION ALL

                SELECT
                    'operation'::text AS source,
                    o.operation AS action,
                    COALESCE(NULLIF(o.destination_path, ''), o.source_path) AS path,
                    o.executed_by AS user_id,
                    0::bigint AS bytes,
                    COALESCE(o.completed_at, o.created_at) AS occurred_at,
                    o.status AS status,
                    o.error AS error
                FROM operations o
                JOIN fs_entry fs ON fs.id = o.fs_entry_id
                WHERE fs.vault_id = $1
            )
            SELECT
                MAX(occurred_at) AS last_activity_at,
                (ARRAY_AGG(action ORDER BY occurred_at DESC))[1] AS last_activity_action,
                COUNT(*) FILTER (WHERE action = 'upload' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS uploads_24h,
                COUNT(*) FILTER (WHERE action = 'upload' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days') AS uploads_7d,
                COUNT(*) FILTER (WHERE action = 'delete' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS deletes_24h,
                COUNT(*) FILTER (WHERE action = 'delete' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days') AS deletes_7d,
                COUNT(*) FILTER (WHERE action = 'rename' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS renames_24h,
                COUNT(*) FILTER (WHERE action = 'rename' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days') AS renames_7d,
                COUNT(*) FILTER (WHERE action = 'move' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS moves_24h,
                COUNT(*) FILTER (WHERE action = 'move' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days') AS moves_7d,
                COUNT(*) FILTER (WHERE action = 'copy' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS copies_24h,
                COUNT(*) FILTER (WHERE action = 'copy' AND status = 'success' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days') AS copies_7d,
                0::bigint AS restores_24h,
                0::bigint AS restores_7d,
                COALESCE(SUM(bytes) FILTER (WHERE action = 'upload' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'), 0) AS bytes_added_24h,
                COALESCE(SUM(bytes) FILTER (WHERE action = 'delete' AND occurred_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'), 0) AS bytes_removed_24h
            FROM activity_events;
        )SQL"
    );

    conn_->prepare("vault_activity.top_users",
        R"SQL(
            WITH activity_events AS (
                SELECT fa.user_id, fa.timestamp AS occurred_at
                FROM file_activity fa
                JOIN files f ON f.fs_entry_id = fa.file_id
                JOIN fs_entry fs ON fs.id = f.fs_entry_id
                WHERE fs.vault_id = $1

                UNION ALL

                SELECT ft.trashed_by AS user_id, ft.trashed_at AS occurred_at
                FROM files_trashed ft
                WHERE ft.vault_id = $1

                UNION ALL

                SELECT o.executed_by AS user_id, COALESCE(o.completed_at, o.created_at) AS occurred_at
                FROM operations o
                JOIN fs_entry fs ON fs.id = o.fs_entry_id
                WHERE fs.vault_id = $1
            )
            SELECT
                ae.user_id,
                u.name AS user_name,
                COUNT(*) AS count
            FROM activity_events ae
            LEFT JOIN users u ON u.id = ae.user_id
            WHERE ae.occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days'
            GROUP BY ae.user_id, u.name
            ORDER BY count DESC, user_name ASC NULLS LAST
            LIMIT 5;
        )SQL"
    );

    conn_->prepare("vault_activity.top_paths",
        R"SQL(
            WITH activity_events AS (
                SELECT
                    CASE WHEN fa.action IN ('created', 'modified') THEN 'upload' ELSE fa.action END AS action,
                    fs.path AS path,
                    COALESCE(f.size_bytes, 0)::bigint AS bytes,
                    fa.timestamp AS occurred_at
                FROM file_activity fa
                JOIN files f ON f.fs_entry_id = fa.file_id
                JOIN fs_entry fs ON fs.id = f.fs_entry_id
                WHERE fs.vault_id = $1

                UNION ALL

                SELECT 'delete'::text AS action, ft.path, COALESCE(ft.size_bytes, 0)::bigint AS bytes, ft.trashed_at AS occurred_at
                FROM files_trashed ft
                WHERE ft.vault_id = $1

                UNION ALL

                SELECT o.operation AS action, COALESCE(NULLIF(o.destination_path, ''), o.source_path) AS path, 0::bigint AS bytes, COALESCE(o.completed_at, o.created_at) AS occurred_at
                FROM operations o
                JOIN fs_entry fs ON fs.id = o.fs_entry_id
                WHERE fs.vault_id = $1
            )
            SELECT
                path,
                action,
                COUNT(*) AS count,
                COALESCE(SUM(bytes), 0) AS bytes
            FROM activity_events
            WHERE occurred_at >= CURRENT_TIMESTAMP - INTERVAL '7 days'
            GROUP BY path, action
            ORDER BY count DESC, bytes DESC, path ASC
            LIMIT 8;
        )SQL"
    );

    conn_->prepare("vault_activity.recent",
        R"SQL(
            WITH activity_events AS (
                SELECT
                    'file_activity'::text AS source,
                    CASE WHEN fa.action IN ('created', 'modified') THEN 'upload' ELSE fa.action END AS action,
                    fs.path AS path,
                    fa.user_id,
                    u.name AS user_name,
                    COALESCE(f.size_bytes, 0)::bigint AS bytes,
                    fa.timestamp AS occurred_at,
                    NULL::text AS status,
                    NULL::text AS error
                FROM file_activity fa
                JOIN files f ON f.fs_entry_id = fa.file_id
                JOIN fs_entry fs ON fs.id = f.fs_entry_id
                LEFT JOIN users u ON u.id = fa.user_id
                WHERE fs.vault_id = $1

                UNION ALL

                SELECT
                    'trash'::text AS source,
                    'delete'::text AS action,
                    ft.path,
                    ft.trashed_by AS user_id,
                    u.name AS user_name,
                    COALESCE(ft.size_bytes, 0)::bigint AS bytes,
                    ft.trashed_at AS occurred_at,
                    'success'::text AS status,
                    NULL::text AS error
                FROM files_trashed ft
                LEFT JOIN users u ON u.id = ft.trashed_by
                WHERE ft.vault_id = $1

                UNION ALL

                SELECT
                    'operation'::text AS source,
                    o.operation AS action,
                    COALESCE(NULLIF(o.destination_path, ''), o.source_path) AS path,
                    o.executed_by AS user_id,
                    u.name AS user_name,
                    0::bigint AS bytes,
                    COALESCE(o.completed_at, o.created_at) AS occurred_at,
                    o.status AS status,
                    o.error AS error
                FROM operations o
                JOIN fs_entry fs ON fs.id = o.fs_entry_id
                LEFT JOIN users u ON u.id = o.executed_by
                WHERE fs.vault_id = $1
            )
            SELECT *
            FROM activity_events
            ORDER BY occurred_at DESC
            LIMIT 12;
        )SQL"
    );
}
