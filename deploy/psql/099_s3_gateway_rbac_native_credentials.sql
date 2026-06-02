ALTER TABLE admin_role
    ADD COLUMN IF NOT EXISTS s3_gateway_permissions BIT(8) NOT NULL DEFAULT B'00000000';

CREATE TABLE IF NOT EXISTS s3_gateway_credential_vault_role_assignment
(
    id                    SERIAL PRIMARY KEY,
    credential_id          INTEGER NOT NULL REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE,
    vault_id               INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    vault_role_id          INTEGER NOT NULL REFERENCES vault_role(id) ON DELETE RESTRICT,
    enabled                BOOLEAN NOT NULL DEFAULT TRUE,
    created_by             INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at             TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (credential_id, vault_id)
);

CREATE TABLE IF NOT EXISTS s3_gateway_credential_vault_role_override
(
    id                                      SERIAL PRIMARY KEY,
    gateway_credential_vault_role_id        INTEGER NOT NULL REFERENCES s3_gateway_credential_vault_role_assignment(id) ON DELETE CASCADE,
    permission_id                           INTEGER NOT NULL REFERENCES permission(id) ON DELETE CASCADE,
    glob_path                               TEXT NOT NULL,
    enabled                                 BOOLEAN NOT NULL DEFAULT TRUE,
    effect                                  VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at                              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at                              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (gateway_credential_vault_role_id, permission_id, glob_path)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_role_assignment_credential
    ON s3_gateway_credential_vault_role_assignment(credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_role_assignment_vault
    ON s3_gateway_credential_vault_role_assignment(vault_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_role_assignment_role
    ON s3_gateway_credential_vault_role_assignment(vault_role_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_role_override_assignment
    ON s3_gateway_credential_vault_role_override(gateway_credential_vault_role_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_role_override_permission
    ON s3_gateway_credential_vault_role_override(permission_id);

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_credential_vault_role_assignment
    ON s3_gateway_credential_vault_role_assignment;
CREATE TRIGGER trg_set_updated_at_s3_gateway_credential_vault_role_assignment
    BEFORE UPDATE ON s3_gateway_credential_vault_role_assignment
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_credential_vault_role_override
    ON s3_gateway_credential_vault_role_override;
CREATE TRIGGER trg_set_updated_at_s3_gateway_credential_vault_role_override
    BEFORE UPDATE ON s3_gateway_credential_vault_role_override
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

INSERT INTO s3_gateway_credential_vault_role_assignment
    (credential_id, vault_id, vault_role_id, enabled, created_by)
SELECT
    s.credential_id,
    s.vault_id,
    vr.id,
    TRUE,
    c.created_by
FROM s3_gateway_credential_vault_scope s
INNER JOIN s3_gateway_credentials c
    ON c.id = s.credential_id
INNER JOIN LATERAL (
    SELECT id
    FROM vault_role
    WHERE name = CASE
        WHEN s.can_admin THEN 'manager'
        WHEN s.can_delete THEN 'manager'
        WHEN s.can_write THEN 'contributor'
        WHEN s.can_read THEN 'reader'
        WHEN s.can_list THEN 'guest'
        ELSE 'implicit_deny'
    END
    LIMIT 1
) vr ON TRUE
ON CONFLICT (credential_id, vault_id) DO NOTHING;

COMMENT ON TABLE s3_gateway_credential_vault_scope IS
    'Deprecated compatibility table. S3 gateway authorization uses s3_gateway_credential_vault_role_assignment.';
