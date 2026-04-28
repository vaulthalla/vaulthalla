#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareSessions() const {
    conn_->prepare("share_session_insert", R"SQL(
        INSERT INTO share_session (
            share_id, session_token_lookup_id, session_token_hash, email_hash,
            verified_at, expires_at, ip_address, user_agent
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
        RETURNING *
    )SQL");

    conn_->prepare("share_session_get", "SELECT * FROM share_session WHERE id = $1");
    conn_->prepare("share_session_get_by_lookup_id", "SELECT * FROM share_session WHERE session_token_lookup_id = $1");
    conn_->prepare("share_session_verify", "UPDATE share_session SET email_hash = $2, verified_at = CURRENT_TIMESTAMP WHERE id = $1 RETURNING id");
    conn_->prepare("share_session_touch", "UPDATE share_session SET last_seen_at = CURRENT_TIMESTAMP WHERE id = $1 RETURNING id");
    conn_->prepare("share_session_revoke", "UPDATE share_session SET revoked_at = CURRENT_TIMESTAMP WHERE id = $1 RETURNING id");
    conn_->prepare("share_session_revoke_for_share", "UPDATE share_session SET revoked_at = CURRENT_TIMESTAMP WHERE share_id = $1 AND revoked_at IS NULL");
    conn_->prepare("share_session_purge_expired", "DELETE FROM share_session WHERE expires_at <= CURRENT_TIMESTAMP RETURNING id");
}
