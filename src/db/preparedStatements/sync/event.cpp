#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSyncEvents() const {
    // ---------------------------------------
    // CREATE (DB generates run_uuid)
    // ---------------------------------------
    conn_->prepare("sync_event.create",
                   R"SQL(
            INSERT INTO sync_event
            (
                vault_id,
                timestamp_begin,
                status,
                trigger,
                retry_attempt,
                config_hash
            )
            VALUES
            (
                $1,
                COALESCE($2, CURRENT_TIMESTAMP),
                COALESCE($3, 'running'),
                COALESCE($4, 'schedule'),
                COALESCE($5, 0),
                $6
            )
            RETURNING id, run_uuid;
        )SQL"
    );

    // ---------------------------------------
    // UPSERT (deterministic by UUID)
    // ---------------------------------------
    conn_->prepare("sync_event.upsert",
                   R"SQL(
            INSERT INTO sync_event
            (
                vault_id,
                run_uuid,
                timestamp_begin,
                timestamp_end,
                status,
                trigger,
                retry_attempt,
                heartbeat_at,
                stall_reason,
                error_code,
                error_message,
                num_ops_total,
                num_failed_ops,
                num_conflicts,
                bytes_up,
                bytes_down,
                divergence_detected,
                local_state_hash,
                remote_state_hash,
                config_hash
            )
            VALUES
            (
                $1,  -- vault_id
                $2,  -- run_uuid
                COALESCE($3, CURRENT_TIMESTAMP),
                $4,
                COALESCE($5, 'running'),
                COALESCE($6, 'schedule'),
                COALESCE($7, 0),
                $8,
                $9,
                $10,
                $11,
                COALESCE($12, 0),
                COALESCE($13, 0),
                COALESCE($14, 0),
                COALESCE($15, 0),
                COALESCE($16, 0),
                COALESCE($17, FALSE),
                $18,
                $19,
                $20
            )
            ON CONFLICT (vault_id, run_uuid)
            DO UPDATE SET
                timestamp_begin      = EXCLUDED.timestamp_begin,
                timestamp_end        = EXCLUDED.timestamp_end,
                status               = EXCLUDED.status,
                trigger              = EXCLUDED.trigger,
                retry_attempt        = EXCLUDED.retry_attempt,
                heartbeat_at         = EXCLUDED.heartbeat_at,
                stall_reason         = EXCLUDED.stall_reason,
                error_code           = EXCLUDED.error_code,
                error_message        = EXCLUDED.error_message,
                num_ops_total        = EXCLUDED.num_ops_total,
                num_failed_ops       = EXCLUDED.num_failed_ops,
                num_conflicts        = EXCLUDED.num_conflicts,
                bytes_up             = EXCLUDED.bytes_up,
                bytes_down           = EXCLUDED.bytes_down,
                divergence_detected  = EXCLUDED.divergence_detected,
                local_state_hash     = EXCLUDED.local_state_hash,
                remote_state_hash    = EXCLUDED.remote_state_hash,
                config_hash          = EXCLUDED.config_hash
            RETURNING id, run_uuid;
        )SQL"
    );

    // ---------------------------------------
    // READ by UUID
    // ---------------------------------------
    conn_->prepare("sync_event.read_by_uuid",
                   R"SQL(
            SELECT *
            FROM sync_event
            WHERE vault_id = $1
              AND run_uuid = $2;
        )SQL"
    );

    // ---------------------------------------
    // LIST runs for vault
    // ---------------------------------------
    conn_->prepare("sync_event.list_for_vault",
                   R"SQL(
            SELECT *
            FROM sync_event
            WHERE vault_id = $1
            ORDER BY timestamp_begin DESC
            LIMIT $2 OFFSET $3;
        )SQL"
    );

    // ---------------------------------------
    // TOUCH HEARTBEAT
    // ---------------------------------------
    conn_->prepare("sync_event.touch_heartbeat",
                   R"SQL(
            UPDATE sync_event
            SET heartbeat_at = COALESCE($3, CURRENT_TIMESTAMP)
            WHERE vault_id = $1
              AND run_uuid = $2
            RETURNING id;
        )SQL"
    );

    // ---------------------------------------
    // FINISH RUN
    // ---------------------------------------
    conn_->prepare("sync_event.finish",
                   R"SQL(
            UPDATE sync_event
            SET
                timestamp_end = COALESCE($3, CURRENT_TIMESTAMP),
                status        = $4,
                stall_reason  = $5,
                error_code    = $6,
                error_message = $7
            WHERE vault_id = $1
              AND run_uuid = $2
            RETURNING id;
        )SQL"
    );

    // ---------------------------------------
    // UPDATE COUNTERS
    // ---------------------------------------
    conn_->prepare("sync_event.update_counters",
                   R"SQL(
            UPDATE sync_event
            SET
                num_ops_total  = $3,
                num_failed_ops = $4,
                num_conflicts  = $5,
                bytes_up       = $6,
                bytes_down     = $7
            WHERE vault_id = $1
              AND run_uuid = $2
            RETURNING id;
        )SQL"
    );

    // ---------------------------------------
    // DELETE RUN
    // ---------------------------------------
    conn_->prepare("sync_event.delete",
                   R"SQL(
            DELETE FROM sync_event
            WHERE vault_id = $1
              AND run_uuid = $2
            RETURNING id;
        )SQL"
    );


    // ---------------------------------------
    // CLEANUP EVENTS
    // ---------------------------------------
    conn_->prepare("sync_event.cleanup", "SELECT vh.cleanup_sync_events($1, $2, $3, $4);");
}