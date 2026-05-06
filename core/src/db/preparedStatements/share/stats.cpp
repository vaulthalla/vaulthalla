#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareStats() const {
    conn_->prepare("share_stats.summary",
        R"SQL(
            SELECT
                COUNT(*) FILTER (
                    WHERE revoked_at IS NULL
                      AND disabled_at IS NULL
                      AND (expires_at IS NULL OR expires_at > CURRENT_TIMESTAMP)
                ) AS active_links,
                COUNT(*) FILTER (
                    WHERE expires_at IS NOT NULL
                      AND expires_at <= CURRENT_TIMESTAMP
                      AND revoked_at IS NULL
                ) AS expired_links,
                COUNT(*) FILTER (WHERE revoked_at IS NOT NULL) AS revoked_links,
                COUNT(*) FILTER (WHERE created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS links_created_24h,
                COUNT(*) FILTER (WHERE revoked_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours') AS links_revoked_24h,
                COUNT(*) FILTER (WHERE access_mode = 'public') AS public_links,
                COUNT(*) FILTER (WHERE access_mode = 'email_validated') AS email_validated_links
            FROM share_link
            WHERE vault_id = $1;
        )SQL"
    );

    conn_->prepare("share_stats.event_window",
        R"SQL(
            SELECT
                (
                    SELECT COUNT(*)
                    FROM share_access_event
                    WHERE vault_id = $1
                      AND event_type LIKE 'share.download%'
                      AND status = 'success'
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS downloads_24h,
                (
                    SELECT COUNT(*)
                    FROM share_upload su
                    JOIN share_link sl ON sl.id = su.share_id
                    WHERE sl.vault_id = $1
                      AND su.status = 'complete'
                      AND su.completed_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS uploads_24h,
                COUNT(*) FILTER (
                    WHERE status = 'denied'
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS denied_attempts_24h,
                COUNT(*) FILTER (
                    WHERE status = 'rate_limited'
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS rate_limited_attempts_24h,
                COUNT(*) FILTER (
                    WHERE status = 'failed'
                      AND created_at >= CURRENT_TIMESTAMP - INTERVAL '24 hours'
                ) AS failed_attempts_24h
            FROM share_access_event
            WHERE vault_id = $1;
        )SQL"
    );

    conn_->prepare("share_stats.top_links",
        R"SQL(
            SELECT
                id::text AS share_id,
                COALESCE(NULLIF(public_label, ''), NULLIF(name, ''), root_path, id::text) AS label,
                COALESCE(root_path, '') AS root_path,
                link_type,
                access_mode,
                access_count,
                download_count,
                upload_count,
                last_accessed_at
            FROM share_link
            WHERE vault_id = $1
            ORDER BY access_count DESC, download_count DESC, upload_count DESC, created_at DESC
            LIMIT 8;
        )SQL"
    );

    conn_->prepare("share_stats.recent_events",
        R"SQL(
            SELECT
                id,
                share_id::text AS share_id,
                event_type,
                status,
                target_path,
                bytes_transferred,
                error_code,
                error_message,
                created_at
            FROM share_access_event
            WHERE vault_id = $1
            ORDER BY created_at DESC
            LIMIT 12;
        )SQL"
    );
}
