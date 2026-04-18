#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSyncConflicts() const {
    // Upsert conflict (insert or update)
    conn_->prepare(
        "sync_conflict.upsert",
        R"SQL(
        INSERT INTO sync_conflicts
            (event_id, file_id, conflict_type, resolution, resolved_at)
        VALUES
            ($1, $2, $3, $4::VARCHAR(24),
             CASE
                 WHEN $4::VARCHAR(24) <> 'unresolved'::VARCHAR(24) THEN NOW()
                 ELSE NULL
             END)
        ON CONFLICT (event_id, file_id)
        DO UPDATE SET
            conflict_type = EXCLUDED.conflict_type,
            resolution    = EXCLUDED.resolution,
            resolved_at   = CASE
                                WHEN EXCLUDED.resolution <> 'unresolved'::VARCHAR(24)
                                THEN NOW()
                                ELSE sync_conflicts.resolved_at
                            END
        RETURNING id
    )SQL"
    );

    // Explicit manual resolution
    conn_->prepare(
        "sync_conflict.resolve",
        R"SQL(
            UPDATE sync_conflicts
               SET resolution  = $1,
                   resolved_at = NOW()
             WHERE id = $2
        )SQL"
    );

    // Select all conflicts for event
    conn_->prepare(
        "sync_conflict.select_by_event",
        R"SQL(
            SELECT id,
                   event_id,
                   file_id,
                   conflict_type,
                   resolution,
                   resolved_at,
                   created_at
              FROM sync_conflicts
             WHERE event_id = $1
        )SQL"
    );

    // Select unresolved conflicts for event (scheduler gate)
    conn_->prepare(
        "sync_conflict.select_unresolved_by_event",
        R"SQL(
            SELECT id,
                   file_id,
                   conflict_type,
                   created_at
              FROM sync_conflicts
             WHERE event_id = $1
               AND resolution = 'unresolved'
        )SQL"
    );

    // Count unresolved conflicts (fast gate check)
    conn_->prepare(
        "sync_conflict.count_unresolved_by_event",
        R"SQL(
            SELECT COUNT(*)
              FROM sync_conflicts
             WHERE event_id = $1
               AND resolution = 'unresolved'
        )SQL"
    );
}