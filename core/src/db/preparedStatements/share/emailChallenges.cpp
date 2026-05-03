#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareEmailChallenges() const {
    conn_->prepare("share_email_challenge_insert", R"SQL(
        INSERT INTO share_email_challenge (
            share_id, share_session_id, email_hash, code_hash, attempts, max_attempts,
            expires_at, ip_address, user_agent
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
        RETURNING *
    )SQL");

    conn_->prepare("share_email_challenge_get", "SELECT * FROM share_email_challenge WHERE id = $1");

    conn_->prepare("share_email_challenge_get_active", R"SQL(
        SELECT *
        FROM share_email_challenge
        WHERE share_id = $1
          AND email_hash = $2
          AND consumed_at IS NULL
          AND expires_at > CURRENT_TIMESTAMP
          AND attempts < max_attempts
        ORDER BY created_at DESC
        LIMIT 1
    )SQL");

    conn_->prepare("share_email_challenge_record_attempt",
                   "UPDATE share_email_challenge SET attempts = attempts + 1 WHERE id = $1 AND attempts < max_attempts AND consumed_at IS NULL AND expires_at > CURRENT_TIMESTAMP RETURNING id");
    conn_->prepare("share_email_challenge_consume",
                   "UPDATE share_email_challenge SET consumed_at = CURRENT_TIMESTAMP WHERE id = $1 AND consumed_at IS NULL AND expires_at > CURRENT_TIMESTAMP RETURNING id");
    conn_->prepare("share_email_challenge_purge_expired",
                   "DELETE FROM share_email_challenge WHERE expires_at <= CURRENT_TIMESTAMP OR consumed_at IS NOT NULL RETURNING id");
}
