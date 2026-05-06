CREATE TABLE IF NOT EXISTS stats_snapshot (
    id BIGSERIAL PRIMARY KEY,
    scope VARCHAR(24) NOT NULL CHECK (scope IN ('system', 'vault')),
    vault_id INTEGER REFERENCES vault(id) ON DELETE CASCADE,
    snapshot_type VARCHAR(32) NOT NULL,
    payload JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (
        (scope = 'system' AND vault_id IS NULL)
        OR
        (scope = 'vault' AND vault_id IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS idx_stats_snapshot_scope_type_created
    ON stats_snapshot (scope, snapshot_type, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_stats_snapshot_vault_type_created
    ON stats_snapshot (vault_id, snapshot_type, created_at DESC)
    WHERE vault_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_stats_snapshot_created
    ON stats_snapshot (created_at DESC);
