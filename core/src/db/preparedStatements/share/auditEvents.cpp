#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareAuditEvents() const {
    conn_->prepare("share_access_event_insert", R"SQL(
        INSERT INTO share_access_event (
            share_id, share_session_id, actor_type, actor_user_id, event_type,
            vault_id, target_entry_id, target_path, status, bytes_transferred,
            error_code, error_message, ip_address, user_agent
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
        RETURNING id
    )SQL");

    conn_->prepare("share_access_event_list_for_share",
                   "SELECT * FROM share_access_event WHERE share_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3");
    conn_->prepare("share_access_event_list_for_vault",
                   "SELECT * FROM share_access_event WHERE vault_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3");
}
