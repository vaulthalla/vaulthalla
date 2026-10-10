-- Sync conflict resolution (#187).
--
-- Under the `ask` remote conflict policy a sync pass records a conflict and leaves the file alone until someone
-- decides. Before this migration nothing could decide: every pass wrote another sync_conflicts row for the same
-- file, the rows died with their sync_event when event retention pruned it, and no command resolved them.
--
--   sync_conflicts.vault_id      the conflict's vault (listing and the console badge filter on it without joining
--                                sync_event). Backfilled from the file.
--   sync_conflicts.updated_at    the last pass that observed the conflict (artifacts refreshed in place).
--   sync_conflicts.event_id      the run that first saw it. Nullable now, ON DELETE SET NULL: pruning old sync
--                                events no longer deletes open conflicts.
--   resolution                   adds 'converged' (both sides became equal on their own; closed by the next pass)
--                                and 'superseded' (closed without a decision: a duplicate, or one side went away).
--   uq_sync_conflicts_open_file  at most one open (unresolved) conflict per file. Older duplicate open rows are
--                                closed as 'superseded' first, keeping the newest.
--   sync_conflict_artifacts      remote_etag and encrypted, so a resolution can check the bucket still holds the
--                                version that was recorded.
--   sync_file_baseline           what each side was when they last agreed (the local row's hash and size, the
--                                remote index row's hash, ETag and size). `ask` uses it to tell a change on one
--                                side (synced normally) from a change on both (a conflict).
--   vault.sync.action.resolve_conflicts
--                                new vault permission, bit 2 of the sync action set (bit 10 of sync_permissions;
--                                trigger is bit 8). Granted once to every vault role and every admin vault-globals
--                                policy that already holds sync trigger, built-in or custom.
--
-- Idempotent and safe on upgraded installs: additive columns/tables/indexes are guarded, constraint swaps are
-- re-runnable, and the permission grant runs only in the same transaction that adds sync_conflicts.vault_id.

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'sync_conflicts'
          AND column_name = 'vault_id'
    ) THEN
        ALTER TABLE sync_conflicts ADD COLUMN vault_id INTEGER REFERENCES vault (id) ON DELETE CASCADE;

        UPDATE sync_conflicts c
        SET vault_id = e.vault_id
        FROM fs_entry e
        WHERE e.id = c.file_id
          AND c.vault_id IS NULL;

        -- Grant resolve_conflicts (bit 10) wherever sync trigger (bit 8) is held. BIT(32) get_bit/set_bit count from
        -- the leftmost bit, so mask bit k is position 31 - k.
        UPDATE vault_role
        SET sync_permissions = set_bit(sync_permissions, 31 - 10, 1)
        WHERE get_bit(sync_permissions, 31 - 8) = 1;

        UPDATE user_global_vault_policy
        SET sync_permissions = set_bit(sync_permissions, 31 - 10, 1)
        WHERE get_bit(sync_permissions, 31 - 8) = 1;
    END IF;
END $$;

ALTER TABLE sync_conflicts ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP;
UPDATE sync_conflicts SET updated_at = COALESCE(resolved_at, created_at, CURRENT_TIMESTAMP) WHERE updated_at IS NULL;

-- event_id: nullable, and ON DELETE SET NULL instead of CASCADE.
ALTER TABLE sync_conflicts ALTER COLUMN event_id DROP NOT NULL;

DO $$
DECLARE
    fk RECORD;
BEGIN
    FOR fk IN
        SELECT conname
        FROM pg_constraint
        WHERE conrelid = 'sync_conflicts'::regclass
          AND contype = 'f'
          AND confrelid = 'sync_event'::regclass
    LOOP
        EXECUTE format('ALTER TABLE sync_conflicts DROP CONSTRAINT %I', fk.conname);
    END LOOP;

    ALTER TABLE sync_conflicts
        ADD CONSTRAINT sync_conflicts_event_id_fkey
        FOREIGN KEY (event_id) REFERENCES sync_event (id) ON DELETE SET NULL;
END $$;

-- resolution: add 'converged' and 'superseded'.
DO $$
DECLARE
    ck RECORD;
BEGIN
    FOR ck IN
        SELECT conname
        FROM pg_constraint
        WHERE conrelid = 'sync_conflicts'::regclass
          AND contype = 'c'
          AND pg_get_constraintdef(oid) LIKE '%resolution%'
    LOOP
        EXECUTE format('ALTER TABLE sync_conflicts DROP CONSTRAINT %I', ck.conname);
    END LOOP;

    ALTER TABLE sync_conflicts
        ADD CONSTRAINT sync_conflicts_resolution_check
        CHECK (resolution IN ('unresolved', 'kept_local', 'kept_remote', 'kept_both', 'overwritten',
                              'fixed_remote_encryption', 'converged', 'superseded'));
END $$;

-- One open conflict per file: close older duplicates (keep the newest row), then enforce it.
UPDATE sync_conflicts c
SET resolution = 'superseded',
    resolved_at = CURRENT_TIMESTAMP,
    updated_at = CURRENT_TIMESTAMP
WHERE c.resolution = 'unresolved'
  AND EXISTS (
      SELECT 1
      FROM sync_conflicts newer
      WHERE newer.file_id = c.file_id
        AND newer.resolution = 'unresolved'
        AND newer.id > c.id
  );

CREATE UNIQUE INDEX IF NOT EXISTS uq_sync_conflicts_open_file
    ON sync_conflicts (file_id)
    WHERE resolution = 'unresolved';

CREATE INDEX IF NOT EXISTS idx_sync_conflicts_open_vault
    ON sync_conflicts (vault_id)
    WHERE resolution = 'unresolved';

ALTER TABLE sync_conflict_artifacts ADD COLUMN IF NOT EXISTS remote_etag TEXT DEFAULT NULL;
ALTER TABLE sync_conflict_artifacts ADD COLUMN IF NOT EXISTS encrypted BOOLEAN DEFAULT NULL;

CREATE TABLE IF NOT EXISTS sync_file_baseline
(
    file_id           INTEGER PRIMARY KEY REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    vault_id          INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    content_hash      VARCHAR(128),
    size_bytes        BIGINT NOT NULL,
    remote_content_hash VARCHAR(128),
    remote_etag       TEXT,
    remote_size_bytes BIGINT,
    synced_at         TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_sync_file_baseline_vault ON sync_file_baseline (vault_id);

-- The permission catalog (permissions.list) is seeded from core only on a fresh database; list the new permission
-- on upgraded ones too (bit position as the fresh seed writes it: sync action set at offset 8, bit 2).
INSERT INTO permission (name, description, category, bit_position)
VALUES ('vault.sync.action.resolve_conflicts',
        'Allows the user to resolve sync conflicts (keep local or keep remote); each file also needs Overwrite.',
        'vault', 10)
ON CONFLICT DO NOTHING;
