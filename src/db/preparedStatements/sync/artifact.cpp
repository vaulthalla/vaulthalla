#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSyncConflictArtifacts() const {
    conn_->prepare(
        "sync_conflict_artifact.upsert",
        R"SQL(
        INSERT INTO sync_conflict_artifacts
            (conflict_id,
             side,
             size_bytes,
             mime_type,
             content_hash,
             last_modified,
             encryption_iv,
             key_version,
             local_backing_path)
        VALUES
            ($1, $2, $3, $4, $5, $6, $7, $8, $9)
        ON CONFLICT (conflict_id, side)
        DO UPDATE SET
            size_bytes         = EXCLUDED.size_bytes,
            mime_type          = EXCLUDED.mime_type,
            content_hash       = EXCLUDED.content_hash,
            last_modified      = EXCLUDED.last_modified,
            encryption_iv      = EXCLUDED.encryption_iv,
            key_version        = EXCLUDED.key_version,
            local_backing_path = COALESCE(EXCLUDED.local_backing_path,
                                          sync_conflict_artifacts.local_backing_path)
        RETURNING id
    )SQL"
    );

    // Update local backing path (if artifact file later cached)
    conn_->prepare(
        "sync_conflict_artifact.update_backing_path",
        R"SQL(
            UPDATE sync_conflict_artifacts
               SET local_backing_path = $1
             WHERE id = $2
        )SQL"
    );

    // Select both artifacts for a conflict
    conn_->prepare(
        "sync_conflict_artifact.select_by_conflict",
        R"SQL(
            SELECT id,
                   conflict_id,
                   side,
                   size_bytes,
                   mime_type,
                   content_hash,
                   last_modified,
                   encryption_iv,
                   key_version,
                   local_backing_path,
                   created_at
              FROM sync_conflict_artifacts
             WHERE conflict_id = $1
        )SQL"
    );

    // Select one side (local or upstream)
    conn_->prepare(
        "sync_conflict_artifact.select_side",
        R"SQL(
            SELECT id,
                   conflict_id,
                   side,
                   size_bytes,
                   mime_type,
                   content_hash,
                   last_modified,
                   encryption_iv,
                   key_version,
                   local_backing_path,
                   created_at
              FROM sync_conflict_artifacts
             WHERE conflict_id = $1
               AND side = $2
        )SQL"
    );

    // Delete all artifacts for a conflict (CASCADE usually handles this, but useful standalone)
    conn_->prepare(
        "sync_conflict_artifact.delete_by_conflict",
        R"SQL(
            DELETE FROM sync_conflict_artifacts
             WHERE conflict_id = $1
        )SQL"
    );

    // Optional: retention cleanup (if you ever do artifact pruning)
    conn_->prepare(
        "sync_conflict_artifact.delete_older_than",
        R"SQL(
            DELETE FROM sync_conflict_artifacts
             WHERE created_at < NOW() - ($1::INTERVAL)
        )SQL"
    );
}