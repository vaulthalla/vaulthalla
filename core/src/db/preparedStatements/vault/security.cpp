#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedVaultSecurity() const {
    conn_->prepare("vault_security.summary",
        R"SQL(
            WITH current_key AS (
                SELECT version, created_at
                FROM vault_keys
                WHERE vault_id = $1
            ),
            file_rollup AS (
                SELECT
                    COUNT(*) AS file_count,
                    COUNT(*) FILTER (
                        WHERE f.encrypted_with_key_version = (SELECT version FROM current_key)
                    ) AS files_current_key_version,
                    COUNT(*) FILTER (
                        WHERE (SELECT version FROM current_key) IS NOT NULL
                          AND f.encrypted_with_key_version < (SELECT version FROM current_key)
                    ) AS files_legacy_key_version,
                    COUNT(*) FILTER (
                        WHERE f.encrypted_with_key_version IS NULL
                           OR ((SELECT version FROM current_key) IS NOT NULL
                               AND f.encrypted_with_key_version > (SELECT version FROM current_key))
                    ) AS files_unknown_key_version
                FROM files f
                JOIN fs_entry fs ON fs.id = f.fs_entry_id
                WHERE fs.vault_id = $1
            ),
            last_denied AS (
                SELECT error_code, error_message, created_at
                FROM share_access_event
                WHERE vault_id = $1
                  AND status IN ('denied', 'rate_limited', 'failed')
                ORDER BY created_at DESC
                LIMIT 1
            )
            SELECT
                (SELECT version FROM current_key) AS current_key_version,
                (SELECT created_at FROM current_key) AS key_created_at,
                CASE
                    WHEN (SELECT created_at FROM current_key) IS NULL THEN NULL
                    ELSE FLOOR(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - (SELECT created_at FROM current_key))) / 86400)::bigint
                END AS key_age_days,
                (SELECT COUNT(*) FROM vault_keys_trashed WHERE vault_id = $1) AS trashed_key_versions_count,
                COALESCE((SELECT file_count FROM file_rollup), 0) AS file_count,
                COALESCE((SELECT files_current_key_version FROM file_rollup), 0) AS files_current_key_version,
                COALESCE((SELECT files_legacy_key_version FROM file_rollup), 0) AS files_legacy_key_version,
                COALESCE((SELECT files_unknown_key_version FROM file_rollup), 0) AS files_unknown_key_version,
                (
                    SELECT COUNT(*)
                    FROM share_access_event
                    WHERE vault_id = $1
                      AND status IN ('denied', 'failed')
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS unauthorized_access_attempts_24h,
                (
                    SELECT COUNT(*)
                    FROM share_access_event
                    WHERE vault_id = $1
                      AND status = 'rate_limited'
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS rate_limited_attempts_24h,
                (SELECT created_at FROM last_denied) AS last_denied_access_at,
                COALESCE((SELECT error_code FROM last_denied), (SELECT error_message FROM last_denied)) AS last_denied_access_reason,
                NULLIF(GREATEST(
                    COALESCE((SELECT MAX(assigned_at) FROM vault_role_assignments WHERE vault_id = $1), '-infinity'::timestamp),
                    COALESCE((
                        SELECT MAX(vpo.updated_at)
                        FROM vault_permission_overrides vpo
                        JOIN vault_role_assignments vra ON vra.id = vpo.assignment_id
                        WHERE vra.vault_id = $1
                    ), '-infinity'::timestamp)
                ), '-infinity'::timestamp) AS last_permission_change_at,
                NULLIF(GREATEST(
                    COALESCE((SELECT MAX(updated_at) FROM share_link WHERE vault_id = $1), '-infinity'::timestamptz),
                    COALESCE((SELECT MAX(updated_at) FROM link_share_vault_role WHERE vault_id = $1), '-infinity'::timestamptz),
                    COALESCE((
                        SELECT MAX(lro.updated_at)
                        FROM link_share_vault_role_override lro
                        JOIN link_share_vault_role lr ON lr.id = lro.link_share_vault_role_id
                        WHERE lr.vault_id = $1
                    ), '-infinity'::timestamptz),
                    COALESCE((SELECT MAX(updated_at) FROM share_vault_role_assignment WHERE vault_id = $1), '-infinity'::timestamptz),
                    COALESCE((
                        SELECT MAX(srao.updated_at)
                        FROM share_vault_role_assignment_override srao
                        JOIN share_vault_role_assignment sra ON sra.id = srao.share_vault_role_assignment_id
                        WHERE sra.vault_id = $1
                    ), '-infinity'::timestamptz)
                ), '-infinity'::timestamptz) AS last_share_policy_change_at;
        )SQL"
    );
}
