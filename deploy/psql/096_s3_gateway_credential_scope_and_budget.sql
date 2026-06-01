ALTER TABLE s3_gateway_credentials
    ADD COLUMN IF NOT EXISTS created_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    ADD COLUMN IF NOT EXISTS principal_user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    ADD COLUMN IF NOT EXISTS scope_mode TEXT NOT NULL DEFAULT 'user_access'
        CHECK (scope_mode IN ('user_access', 'global', 'vault_allowlist')),
    ADD COLUMN IF NOT EXISTS description TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS expires_at TIMESTAMP DEFAULT NULL;

UPDATE s3_gateway_credentials
SET principal_user_id = user_id
WHERE principal_user_id IS NULL;

UPDATE s3_gateway_credentials
SET created_by = user_id
WHERE created_by IS NULL;

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_principal_user
    ON s3_gateway_credentials(principal_user_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_created_by
    ON s3_gateway_credentials(created_by);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credentials_scope_mode
    ON s3_gateway_credentials(scope_mode);

CREATE TABLE IF NOT EXISTS s3_gateway_credential_vault_scope
(
    credential_id INTEGER NOT NULL REFERENCES s3_gateway_credentials(id) ON DELETE CASCADE,
    vault_id      INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    can_list      BOOLEAN NOT NULL DEFAULT TRUE,
    can_read      BOOLEAN NOT NULL DEFAULT TRUE,
    can_write     BOOLEAN NOT NULL DEFAULT FALSE,
    can_delete    BOOLEAN NOT NULL DEFAULT FALSE,
    can_admin     BOOLEAN NOT NULL DEFAULT FALSE,
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (credential_id, vault_id)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_scope_credential
    ON s3_gateway_credential_vault_scope(credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_scope_vault
    ON s3_gateway_credential_vault_scope(vault_id);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_credential_vault_scope_credential_vault
    ON s3_gateway_credential_vault_scope(credential_id, vault_id);

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
    ADD COLUMN IF NOT EXISTS estimated_cost NUMERIC(20,8) DEFAULT NULL;

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential
    ON s3_price_budget_ledger(gateway_credential_id);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential_window
    ON s3_price_budget_ledger(gateway_credential_id, window_type, window_start, status);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_gateway_credential_vault_window
    ON s3_price_budget_ledger(gateway_credential_id, vault_id, window_type, window_start, status);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_request_uuid
    ON s3_price_budget_ledger(request_uuid);
