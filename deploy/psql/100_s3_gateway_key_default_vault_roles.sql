CREATE TABLE IF NOT EXISTS s3_gateway_credential_default_vault_role
(
    id              SERIAL PRIMARY KEY,
    credential_id   INTEGER UNIQUE NOT NULL REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE,
    vault_role_id   INTEGER NOT NULL REFERENCES vault_role(id) ON DELETE RESTRICT,
    enabled         BOOLEAN NOT NULL DEFAULT TRUE,
    created_by      INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS s3_gateway_credential_default_vault_role_override
(
    id                                      SERIAL PRIMARY KEY,
    gateway_credential_default_role_id      INTEGER NOT NULL REFERENCES s3_gateway_credential_default_vault_role(id) ON DELETE CASCADE,
    permission_id                           INTEGER NOT NULL REFERENCES permission(id) ON DELETE CASCADE,
    glob_path                               TEXT NOT NULL,
    enabled                                 BOOLEAN NOT NULL DEFAULT TRUE,
    effect                                  VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at                              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at                              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (gateway_credential_default_role_id, permission_id, glob_path)
);

CREATE TABLE IF NOT EXISTS s3_gateway_credential_selected_vault
(
    credential_id   INTEGER NOT NULL REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE,
    vault_id        INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    enabled         BOOLEAN NOT NULL DEFAULT TRUE,
    created_by      INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (credential_id, vault_id)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_default_vault_role_credential
    ON s3_gateway_credential_default_vault_role(credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_default_vault_role_role
    ON s3_gateway_credential_default_vault_role(vault_role_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_default_vault_role_override_role
    ON s3_gateway_credential_default_vault_role_override(gateway_credential_default_role_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_default_vault_role_override_permission
    ON s3_gateway_credential_default_vault_role_override(permission_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_selected_vault_credential
    ON s3_gateway_credential_selected_vault(credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_selected_vault_vault
    ON s3_gateway_credential_selected_vault(vault_id);

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_credential_default_vault_role
    ON s3_gateway_credential_default_vault_role;
CREATE TRIGGER trg_set_updated_at_s3_gateway_credential_default_vault_role
    BEFORE UPDATE ON s3_gateway_credential_default_vault_role
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_credential_default_vault_role_override
    ON s3_gateway_credential_default_vault_role_override;
CREATE TRIGGER trg_set_updated_at_s3_gateway_credential_default_vault_role_override
    BEFORE UPDATE ON s3_gateway_credential_default_vault_role_override
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_credential_selected_vault
    ON s3_gateway_credential_selected_vault;
CREATE TRIGGER trg_set_updated_at_s3_gateway_credential_selected_vault
    BEFORE UPDATE ON s3_gateway_credential_selected_vault
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

INSERT INTO s3_gateway_credential_selected_vault
    (credential_id, vault_id, enabled, created_by)
SELECT DISTINCT
    c.id,
    source.vault_id,
    TRUE,
    c.created_by
FROM s3_gateway_credentials c
INNER JOIN (
    SELECT credential_id, vault_id
    FROM s3_gateway_credential_vault_role_assignment
    WHERE enabled = TRUE

    UNION

    SELECT credential_id, vault_id
    FROM s3_gateway_credential_vault_scope
) source
    ON source.credential_id = c.id
WHERE c.scope_mode = 'vault_allowlist'
ON CONFLICT (credential_id, vault_id) DO UPDATE SET
    enabled = TRUE,
    created_by = COALESCE(EXCLUDED.created_by, s3_gateway_credential_selected_vault.created_by),
    updated_at = CURRENT_TIMESTAMP;

WITH safe_role AS (
    SELECT id
    FROM vault_role
    WHERE name = 'implicit_deny'
    LIMIT 1
),
allowlist_role_counts AS (
    SELECT
        c.id AS credential_id,
        c.created_by,
        COUNT(DISTINCT a.vault_role_id) FILTER (WHERE a.enabled = TRUE) AS distinct_role_count,
        MIN(a.vault_role_id) FILTER (WHERE a.enabled = TRUE) AS only_role_id
    FROM s3_gateway_credentials c
    LEFT JOIN s3_gateway_credential_vault_role_assignment a
        ON a.credential_id = c.id
       AND a.enabled = TRUE
    WHERE c.scope_mode = 'vault_allowlist'
    GROUP BY c.id, c.created_by
),
least_privileged_assignment AS (
    SELECT DISTINCT ON (a.credential_id)
        a.credential_id,
        a.vault_role_id
    FROM s3_gateway_credential_vault_role_assignment a
    INNER JOIN vault_role vr
        ON vr.id = a.vault_role_id
    WHERE a.enabled = TRUE
    ORDER BY
        a.credential_id,
        CASE vr.name
            WHEN 'implicit_deny' THEN 0
            WHEN 'guest' THEN 1
            WHEN 'reader' THEN 2
            WHEN 'contributor' THEN 3
            WHEN 'editor' THEN 4
            WHEN 'manager' THEN 5
            WHEN 'power_user' THEN 6
            WHEN 'full' THEN 7
            ELSE 8
        END,
        vr.id
)
INSERT INTO s3_gateway_credential_default_vault_role
    (credential_id, vault_role_id, enabled, created_by)
SELECT
    counts.credential_id,
    CASE
        WHEN counts.distinct_role_count = 1 THEN counts.only_role_id
        ELSE COALESCE((SELECT id FROM safe_role), least.vault_role_id)
    END,
    TRUE,
    counts.created_by
FROM allowlist_role_counts counts
LEFT JOIN least_privileged_assignment least
    ON least.credential_id = counts.credential_id
WHERE CASE
        WHEN counts.distinct_role_count = 1 THEN counts.only_role_id
        ELSE COALESCE((SELECT id FROM safe_role), least.vault_role_id)
    END IS NOT NULL
ON CONFLICT (credential_id) DO NOTHING;

INSERT INTO s3_gateway_credential_default_vault_role
    (credential_id, vault_role_id, enabled, created_by)
SELECT
    c.id,
    COALESCE(implicit_deny.id, reader.id),
    TRUE,
    c.created_by
FROM s3_gateway_credentials c
LEFT JOIN LATERAL (
    SELECT id
    FROM vault_role
    WHERE name = 'implicit_deny'
    LIMIT 1
) implicit_deny ON TRUE
LEFT JOIN LATERAL (
    SELECT id
    FROM vault_role
    WHERE name = 'reader'
    LIMIT 1
) reader ON TRUE
WHERE c.scope_mode = 'global'
  AND COALESCE(implicit_deny.id, reader.id) IS NOT NULL
ON CONFLICT (credential_id) DO NOTHING;

COMMENT ON TABLE s3_gateway_credential_default_vault_role IS
    'Default vault role for an S3 gateway credential. Applies to selected vaults for vault_allowlist and to gateway bucket bindings for global credentials.';

COMMENT ON TABLE s3_gateway_credential_default_vault_role_override IS
    'Default/key-level S3 gateway credential vault-role path overrides. Per-vault overrides may narrow or replace these for a specific vault.';

COMMENT ON TABLE s3_gateway_credential_selected_vault IS
    'Selected vault set for S3 gateway vault_allowlist credentials. Bucket bindings are routing topology and do not grant credential access.';
