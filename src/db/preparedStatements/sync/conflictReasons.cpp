#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSyncConflictReasons() const {

    // ------------------------------------------------------------
    // UPSERT (conflict_id + reason_code unique)
    // ------------------------------------------------------------
    conn_->prepare(
        "sync_conflict_reason.upsert",
        R"SQL(
            INSERT INTO sync_conflict_reasons (
                conflict_id,
                reason_code,
                reason_message
            )
            VALUES ($1, $2, $3)
            ON CONFLICT (conflict_id, reason_code)
            DO UPDATE SET
                reason_message = EXCLUDED.reason_message
            RETURNING id
        )SQL"
    );

    // ------------------------------------------------------------
    // SELECT by conflict_id
    // ------------------------------------------------------------
    conn_->prepare(
        "sync_conflict_reason.select_by_conflict",
        R"SQL(
            SELECT
                id,
                conflict_id,
                reason_code,
                reason_message,
                created_at
            FROM sync_conflict_reasons
            WHERE conflict_id = $1
            ORDER BY id ASC
        )SQL"
    );

    // ------------------------------------------------------------
    // DELETE by conflict_id (cleanup cascade alternative)
    // ------------------------------------------------------------
    conn_->prepare(
        "sync_conflict_reason.delete_by_conflict",
        R"SQL(
            DELETE FROM sync_conflict_reasons
            WHERE conflict_id = $1
        )SQL"
    );

    // ------------------------------------------------------------
    // DELETE single reason (rare but useful)
    // ------------------------------------------------------------
    conn_->prepare(
        "sync_conflict_reason.delete_one",
        R"SQL(
            DELETE FROM sync_conflict_reasons
            WHERE conflict_id = $1
              AND reason_code = $2
        )SQL"
    );

    // ------------------------------------------------------------
    // COUNT reasons for conflict (dashboard shortcut)
    // ------------------------------------------------------------
    conn_->prepare(
        "sync_conflict_reason.count_by_conflict",
        R"SQL(
            SELECT COUNT(*)
            FROM sync_conflict_reasons
            WHERE conflict_id = $1
        )SQL"
    );
}
