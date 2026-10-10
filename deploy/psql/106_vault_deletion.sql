-- Safe vault deletion with retention (#162).
--
-- Deleting a vault used to delete its rows at once and leave its backing data on disk or in the bucket, with no
-- cleanup path. A delete is now a schedule, carried out by the daemon's VaultRetentionService:
--
--   vault.deleted_at        set when the vault is deleted. The row, and everything that cascades from it (files,
--                           keys, roles, shares, sync state), stays until the purge, so a restore brings back exactly
--                           what was there. Every path that lists or resolves vaults reads deleted_at IS NULL only.
--                           The name, slug, FUSE name and S3 bucket binding stay reserved until the purge.
--   vault_deletion          one row per deleted vault: the schedule (purge_after, key_retain_until), the choices
--                           (delete_upstream), progress (state, attempts, last_error) and, after the purge, the
--                           tombstone (name, owner, key version, timestamps). No foreign key to vault on purpose:
--                           it outlives the vault row.
--   vault_deletion_key      the vault's key versions, still sealed by the TPM master key exactly as in vault_keys,
--                           copied at delete time and kept until key_retain_until so a key can be exported after the
--                           data is gone. Removed by the service when the window ends; the tombstone row stays.
--   vault_keys.exported_*   the key version last exported (vh vault keys export) and when, so the delete flows can
--                           warn about a key that was never exported. Installs upgraded from older releases have no
--                           record of earlier exports.
--
-- Idempotent and safe on upgraded installs: only additive columns, tables and indexes, all guarded. Existing vaults
-- stay live (deleted_at NULL).

ALTER TABLE vault ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMPTZ DEFAULT NULL;

CREATE INDEX IF NOT EXISTS idx_vault_deleted_at ON vault (deleted_at) WHERE deleted_at IS NOT NULL;

ALTER TABLE vault_keys ADD COLUMN IF NOT EXISTS exported_at TIMESTAMPTZ DEFAULT NULL;
ALTER TABLE vault_keys ADD COLUMN IF NOT EXISTS exported_version INTEGER DEFAULT NULL;

CREATE TABLE IF NOT EXISTS vault_deletion
(
    vault_id           INTEGER PRIMARY KEY,
    vault_name         TEXT NOT NULL,
    owner_id           INTEGER REFERENCES users (id) ON DELETE SET NULL,
    owner_name         TEXT,
    vault_type         VARCHAR(12) NOT NULL CHECK (vault_type IN ('local', 's3')),
    backing_alias      TEXT NOT NULL,
    provider           TEXT,
    bucket             TEXT,
    encrypt_upstream   BOOLEAN,
    delete_upstream    BOOLEAN NOT NULL DEFAULT FALSE,
    deleted_by         INTEGER REFERENCES users (id) ON DELETE SET NULL,
    deleted_at         TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    purge_after        TIMESTAMPTZ NOT NULL,
    key_retain_until   TIMESTAMPTZ NOT NULL,
    key_version        INTEGER,
    key_exported_at    TIMESTAMPTZ,
    state              TEXT NOT NULL DEFAULT 'pending' CHECK (state IN ('pending', 'purging', 'purged')),
    purge_started_at   TIMESTAMPTZ,
    upstream_purged_at TIMESTAMPTZ,
    purged_at          TIMESTAMPTZ,
    key_purged_at      TIMESTAMPTZ,
    attempts           INTEGER NOT NULL DEFAULT 0,
    next_attempt_at    TIMESTAMPTZ,
    last_error         TEXT
);

CREATE INDEX IF NOT EXISTS idx_vault_deletion_due ON vault_deletion (state, purge_after);

CREATE TABLE IF NOT EXISTS vault_deletion_key
(
    vault_id      INTEGER NOT NULL REFERENCES vault_deletion (vault_id) ON DELETE CASCADE,
    version       INTEGER NOT NULL,
    encrypted_key BYTEA NOT NULL,
    iv            BYTEA NOT NULL,
    created_at    TIMESTAMPTZ,
    PRIMARY KEY (vault_id, version)
);
