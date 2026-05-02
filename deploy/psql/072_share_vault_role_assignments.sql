-- ##################################
-- RBAC-native Link Share Role Assignments
-- ##################################

CREATE TABLE IF NOT EXISTS share_validated_recipient
(
    id          SERIAL PRIMARY KEY,
    share_id    UUID NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    email_hash  BYTEA NOT NULL,
    enabled     BOOLEAN NOT NULL DEFAULT TRUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (share_id, email_hash)
);

CREATE TABLE IF NOT EXISTS share_vault_role_assignment
(
    id             SERIAL PRIMARY KEY,
    share_id       UUID NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    vault_id       INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    vault_role_id  INTEGER NOT NULL REFERENCES vault_role (id) ON DELETE RESTRICT,
    subject_type   VARCHAR(16) NOT NULL CHECK (subject_type IN ('public', 'actor')),
    subject_id     INTEGER NOT NULL DEFAULT 0,
    enabled        BOOLEAN NOT NULL DEFAULT TRUE,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (share_id, subject_type, subject_id)
);

CREATE TABLE IF NOT EXISTS share_vault_role_assignment_override
(
    id                               SERIAL PRIMARY KEY,
    share_vault_role_assignment_id   INTEGER NOT NULL REFERENCES share_vault_role_assignment (id) ON DELETE CASCADE,
    permission_id                    INTEGER NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    glob_path                        TEXT NOT NULL,
    enabled                          BOOLEAN NOT NULL DEFAULT TRUE,
    effect                           VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at                       TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at                       TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (share_vault_role_assignment_id, permission_id, glob_path)
);

CREATE INDEX IF NOT EXISTS idx_share_validated_recipient_share
    ON share_validated_recipient (share_id);

CREATE INDEX IF NOT EXISTS idx_share_vault_role_assignment_share
    ON share_vault_role_assignment (share_id);

CREATE INDEX IF NOT EXISTS idx_share_vault_role_assignment_role
    ON share_vault_role_assignment (vault_role_id);

CREATE INDEX IF NOT EXISTS idx_share_vault_role_assignment_subject
    ON share_vault_role_assignment (subject_type, subject_id);

CREATE UNIQUE INDEX IF NOT EXISTS idx_share_vault_role_assignment_public
    ON share_vault_role_assignment (share_id)
    WHERE subject_type = 'public';

CREATE INDEX IF NOT EXISTS idx_share_vault_role_assignment_override_assignment
    ON share_vault_role_assignment_override (share_vault_role_assignment_id);

CREATE INDEX IF NOT EXISTS idx_share_vault_role_assignment_override_permission
    ON share_vault_role_assignment_override (permission_id);

DROP TRIGGER IF EXISTS trg_set_updated_at_share_validated_recipient ON share_validated_recipient;
CREATE TRIGGER trg_set_updated_at_share_validated_recipient
    BEFORE UPDATE ON share_validated_recipient
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_share_vault_role_assignment ON share_vault_role_assignment;
CREATE TRIGGER trg_set_updated_at_share_vault_role_assignment
    BEFORE UPDATE ON share_vault_role_assignment
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_share_vault_role_assignment_override ON share_vault_role_assignment_override;
CREATE TRIGGER trg_set_updated_at_share_vault_role_assignment_override
    BEFORE UPDATE ON share_vault_role_assignment_override
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
