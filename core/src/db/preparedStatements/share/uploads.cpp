#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareUploads() const {
    conn_->prepare("share_upload_insert", R"SQL(
        INSERT INTO share_upload (
            share_id, share_session_id, target_parent_entry_id, target_path, tmp_path,
            original_filename, resolved_filename, expected_size_bytes, received_size_bytes,
            mime_type, content_hash, created_entry_id, status, error
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
        RETURNING *
    )SQL");

    conn_->prepare("share_upload_get", "SELECT * FROM share_upload WHERE id = $1");
    conn_->prepare("share_upload_add_received_bytes", R"SQL(
        UPDATE share_upload
        SET received_size_bytes = received_size_bytes + $2,
            status = CASE WHEN status = 'pending' THEN 'receiving' ELSE status END
        WHERE id = $1
          AND status IN ('pending', 'receiving')
          AND received_size_bytes + $2 <= expected_size_bytes
        RETURNING id
    )SQL");
    conn_->prepare("share_upload_complete", R"SQL(
        UPDATE share_upload
        SET status = 'complete',
            created_entry_id = $2,
            content_hash = $3,
            mime_type = $4,
            completed_at = CURRENT_TIMESTAMP
        WHERE id = $1 AND status IN ('pending', 'receiving')
        RETURNING id
    )SQL");
    conn_->prepare("share_upload_fail",
                   "UPDATE share_upload SET status = 'failed', error = $2, completed_at = CURRENT_TIMESTAMP WHERE id = $1 AND status IN ('pending', 'receiving') RETURNING id");
    conn_->prepare("share_upload_cancel",
                   "UPDATE share_upload SET status = 'cancelled', completed_at = CURRENT_TIMESTAMP WHERE id = $1 AND status IN ('pending', 'receiving') RETURNING id");
    conn_->prepare("share_upload_sum_completed_bytes",
                   "SELECT COALESCE(SUM(received_size_bytes), 0) FROM share_upload WHERE share_id = $1 AND status = 'complete'");
    conn_->prepare("share_upload_count_completed_files",
                   "SELECT COUNT(*) FROM share_upload WHERE share_id = $1 AND status = 'complete'");
    conn_->prepare("share_upload_list_for_share",
                   "SELECT * FROM share_upload WHERE share_id = $1 ORDER BY started_at DESC LIMIT $2 OFFSET $3");
    conn_->prepare("share_upload_list_stale_active", R"SQL(
        SELECT *
        FROM share_upload
        WHERE status IN ('pending', 'receiving')
          AND started_at < CURRENT_TIMESTAMP - ($1::bigint * INTERVAL '1 second')
        ORDER BY started_at ASC
        LIMIT $2
    )SQL");
}
