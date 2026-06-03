CREATE TABLE IF NOT EXISTS s3_gateway_bucket
(
    vault_id       INTEGER PRIMARY KEY REFERENCES vault(id) ON DELETE CASCADE,
    bucket_name    TEXT UNIQUE NOT NULL,
    api_exclusive  BOOLEAN NOT NULL DEFAULT FALSE,
    mode           VARCHAR(16) NOT NULL DEFAULT 'local'
        CHECK (mode IN ('local', 'remote_cache', 'remote_proxy')),
    created_by     INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_bucket_name
    ON s3_gateway_bucket(bucket_name);

DROP TRIGGER IF EXISTS trg_set_updated_at_s3_gateway_bucket
    ON s3_gateway_bucket;
CREATE TRIGGER trg_set_updated_at_s3_gateway_bucket
    BEFORE UPDATE ON s3_gateway_bucket
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TABLE IF NOT EXISTS s3_gateway_credentials
(
    id                                  SERIAL PRIMARY KEY,
    user_id                             INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_by                          INTEGER REFERENCES users(id) ON DELETE SET NULL,
    principal_user_id                   INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name                                VARCHAR(100) NOT NULL,
    access_key                          TEXT UNIQUE NOT NULL,
    encrypted_secret_access_key         BYTEA NOT NULL,
    iv                                  BYTEA NOT NULL,
    enabled                             BOOLEAN NOT NULL DEFAULT TRUE,
    enforce_budget_for_local_requests   BOOLEAN NOT NULL DEFAULT FALSE,
    scope_mode                          TEXT NOT NULL DEFAULT 'user_access'
        CHECK (scope_mode IN ('user_access', 'global', 'vault_allowlist')),
    description                         TEXT DEFAULT NULL,
    created_at                          TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_used_at                        TIMESTAMPTZ DEFAULT NULL,
    expires_at                          TIMESTAMPTZ DEFAULT NULL,
    UNIQUE(user_id, name)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_principal_user
    ON s3_gateway_credentials(principal_user_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_created_by
    ON s3_gateway_credentials(created_by);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_scope_mode
    ON s3_gateway_credentials(scope_mode);

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

CREATE TABLE IF NOT EXISTS s3_gateway_credential_vault_role_assignment
(
    id              SERIAL PRIMARY KEY,
    credential_id   INTEGER NOT NULL REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE,
    vault_id        INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    vault_role_id   INTEGER NOT NULL REFERENCES vault_role(id) ON DELETE RESTRICT,
    enabled         BOOLEAN NOT NULL DEFAULT TRUE,
    created_by      INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
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

CREATE TABLE IF NOT EXISTS s3_gateway_object
(
    vault_id       INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key     TEXT NOT NULL,
    etag           TEXT NOT NULL,
    size_bytes     BIGINT NOT NULL DEFAULT 0,
    content_type   TEXT DEFAULT NULL,
    storage_class  TEXT DEFAULT NULL,
    last_modified  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    multipart      BOOLEAN NOT NULL DEFAULT FALSE,
    part_count     INTEGER DEFAULT NULL,
    PRIMARY KEY (vault_id, object_key)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_object_vault_key
    ON s3_gateway_object(vault_id, object_key text_pattern_ops);

CREATE TABLE IF NOT EXISTS s3_gateway_object_metadata
(
    vault_id    INTEGER NOT NULL,
    object_key  TEXT NOT NULL,
    name        TEXT NOT NULL,
    value       TEXT NOT NULL,
    PRIMARY KEY (vault_id, object_key, name),
    FOREIGN KEY (vault_id, object_key)
        REFERENCES s3_gateway_object(vault_id, object_key)
        ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS s3_gateway_multipart_upload
(
    upload_id     TEXT PRIMARY KEY,
    parts_dir_id  TEXT NOT NULL,
    vault_id      INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key    TEXT NOT NULL,
    initiated_by  INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    initiated_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    content_type  TEXT DEFAULT NULL,
    metadata      JSONB NOT NULL DEFAULT '{}'::jsonb,
    storage_class TEXT DEFAULT NULL,
    aborted       BOOLEAN NOT NULL DEFAULT FALSE,
    completed     BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_multipart_upload_vault_key
    ON s3_gateway_multipart_upload(vault_id, object_key);

CREATE UNIQUE INDEX IF NOT EXISTS idx_s3_gateway_multipart_upload_parts_dir_id
    ON s3_gateway_multipart_upload(parts_dir_id);

CREATE TABLE IF NOT EXISTS s3_gateway_multipart_part
(
    upload_id   TEXT NOT NULL REFERENCES s3_gateway_multipart_upload(upload_id) ON DELETE CASCADE,
    part_number INTEGER NOT NULL CHECK (part_number BETWEEN 1 AND 10000),
    etag        TEXT NOT NULL,
    size_bytes  BIGINT NOT NULL,
    md5         BYTEA NOT NULL,
    path        TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(upload_id, part_number)
);

CREATE TABLE IF NOT EXISTS s3_gateway_sync_origin
(
    id SERIAL PRIMARY KEY,
    vault_id INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key TEXT NOT NULL,
    operation TEXT NOT NULL,
    gateway_credential_id INTEGER REFERENCES s3_gateway_credentials(id) ON DELETE SET NULL,
    request_uuid TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    consumed_at TIMESTAMPTZ DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_sync_origin_vault_object
    ON s3_gateway_sync_origin(vault_id, object_key, consumed_at);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_sync_origin_request_uuid
    ON s3_gateway_sync_origin(request_uuid);

ALTER TABLE s3_price_budget_policy
    DROP CONSTRAINT IF EXISTS s3_price_budget_policy_scope_check;

ALTER TABLE s3_price_budget_policy
    ADD CONSTRAINT s3_price_budget_policy_scope_check
    CHECK (scope IN ('global', 'provider', 'vault', 'gateway_credential', 'gateway_credential_vault'));

ALTER TABLE s3_price_budget_policy
    ADD COLUMN IF NOT EXISTS gateway_credential_id INTEGER
        REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE;

ALTER TABLE s3_price_budget_policy
    DROP CONSTRAINT IF EXISTS s3_price_budget_policy_check;

ALTER TABLE s3_price_budget_policy
    ADD CONSTRAINT s3_price_budget_policy_check
    CHECK (
        (scope = 'global' AND provider_key IS NULL AND vault_id IS NULL AND gateway_credential_id IS NULL) OR
        (scope = 'provider' AND provider_key IS NOT NULL AND vault_id IS NULL AND gateway_credential_id IS NULL) OR
        (scope = 'vault' AND vault_id IS NOT NULL AND gateway_credential_id IS NULL) OR
        (scope = 'gateway_credential' AND gateway_credential_id IS NOT NULL AND vault_id IS NULL) OR
        (scope = 'gateway_credential_vault' AND gateway_credential_id IS NOT NULL AND vault_id IS NOT NULL)
    );

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_policy_gateway_credential
    ON s3_price_budget_policy(gateway_credential_id);

CREATE UNIQUE INDEX IF NOT EXISTS ux_s3_price_budget_policy_gateway_credential
    ON s3_price_budget_policy(scope, gateway_credential_id, COALESCE(provider_key, ''))
    WHERE scope = 'gateway_credential';

CREATE UNIQUE INDEX IF NOT EXISTS ux_s3_price_budget_policy_gateway_credential_vault
    ON s3_price_budget_policy(scope, gateway_credential_id, vault_id, COALESCE(provider_key, ''))
    WHERE scope = 'gateway_credential_vault';

ALTER TABLE s3_price_budget_ledger
    ADD COLUMN IF NOT EXISTS gateway_credential_id INTEGER
        REFERENCES s3_gateway_credentials(id) ON DELETE SET NULL,
    ADD COLUMN IF NOT EXISTS request_uuid TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS operation TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS object_key TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS estimated_cost NUMERIC(20,8) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS usage_source TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS synthetic BOOLEAN NOT NULL DEFAULT FALSE;

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential
    ON s3_price_budget_ledger(gateway_credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential_window
    ON s3_price_budget_ledger(gateway_credential_id, window_type, window_start, status);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential_vault_window
    ON s3_price_budget_ledger(gateway_credential_id, vault_id, window_type, window_start, status);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_request_uuid
    ON s3_price_budget_ledger(request_uuid);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_synthetic_gateway
    ON s3_price_budget_ledger(gateway_credential_id, synthetic, status);

COMMENT ON TABLE s3_gateway_bucket IS
    'S3 Gateway bucket binding/routing table. Downstream bucket names route to Vaulthalla vaults and do not grant credential access.';

COMMENT ON TABLE s3_gateway_credentials IS
    'Inbound S3 Gateway credentials. The principal user is the effective Vaulthalla user for RBAC evaluation.';

COMMENT ON COLUMN s3_gateway_credentials.scope_mode IS
    'S3 Gateway credential scope mode: user_access, vault_allowlist, or global.';

COMMENT ON TABLE s3_gateway_credential_default_vault_role IS
    'Required key-level default vault role for role-based S3 Gateway credentials.';

COMMENT ON TABLE s3_gateway_credential_default_vault_role_override IS
    'Key-level S3 Gateway credential vault-role path overrides.';

COMMENT ON TABLE s3_gateway_credential_selected_vault IS
    'Selected vault set for S3 Gateway vault_allowlist credentials.';

COMMENT ON TABLE s3_gateway_credential_vault_role_assignment IS
    'Optional per-vault role exceptions for S3 Gateway credentials.';

COMMENT ON TABLE s3_gateway_credential_vault_role_override IS
    'Optional per-vault path overrides for S3 Gateway credential role exceptions.';

COMMENT ON TABLE s3_gateway_object IS
    'S3 Gateway object state used for object metadata, listing, and remote-cache coordination.';

COMMENT ON TABLE s3_gateway_multipart_upload IS
    'S3 Gateway multipart upload state with opaque part directory identifiers.';

COMMENT ON TABLE s3_gateway_sync_origin IS
    'Origin tracking for local-first S3 Gateway work handed to sync.';
