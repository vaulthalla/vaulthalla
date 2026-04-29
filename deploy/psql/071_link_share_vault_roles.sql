-- ##################################
-- Durable Link Share Vault Roles
-- ##################################

CREATE TABLE IF NOT EXISTS link_share_vault_role
(
    id             SERIAL PRIMARY KEY,
    share_id       UUID UNIQUE NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    vault_id       INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    vault_role_id  INTEGER UNIQUE NOT NULL REFERENCES vault_role (id) ON DELETE CASCADE,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS link_share_vault_role_override
(
    id                         SERIAL PRIMARY KEY,
    link_share_vault_role_id   INTEGER NOT NULL REFERENCES link_share_vault_role (id) ON DELETE CASCADE,
    permission_id              INTEGER NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    glob_path                  TEXT NOT NULL,
    enabled                    BOOLEAN NOT NULL DEFAULT TRUE,
    effect                     VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at                 TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at                 TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (link_share_vault_role_id, permission_id, glob_path)
);

CREATE INDEX IF NOT EXISTS idx_link_share_vault_role_share
    ON link_share_vault_role (share_id);

CREATE INDEX IF NOT EXISTS idx_link_share_vault_role_vault
    ON link_share_vault_role (vault_id);

CREATE INDEX IF NOT EXISTS idx_link_share_vault_role_override_role
    ON link_share_vault_role_override (link_share_vault_role_id);

CREATE INDEX IF NOT EXISTS idx_link_share_vault_role_override_perm
    ON link_share_vault_role_override (permission_id);

DROP TRIGGER IF EXISTS trg_set_updated_at_link_share_vault_role ON link_share_vault_role;
CREATE TRIGGER trg_set_updated_at_link_share_vault_role
    BEFORE UPDATE ON link_share_vault_role
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_link_share_vault_role_override ON link_share_vault_role_override;
CREATE TRIGGER trg_set_updated_at_link_share_vault_role_override
    BEFORE UPDATE ON link_share_vault_role_override
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
