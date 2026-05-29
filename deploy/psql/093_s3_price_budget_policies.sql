CREATE TABLE IF NOT EXISTS s3_price_budget_policy
(
    id SERIAL PRIMARY KEY,
    scope VARCHAR NOT NULL CHECK (scope IN ('global', 'provider', 'vault')),
    provider_key TEXT DEFAULT NULL,
    vault_id INTEGER DEFAULT NULL REFERENCES vault(id) ON DELETE CASCADE,
    mode VARCHAR NOT NULL DEFAULT 'off' CHECK (mode IN ('off', 'report', 'warn', 'enforce')),
    currency VARCHAR(8) NOT NULL DEFAULT 'USD',
    max_run_cost NUMERIC(20,8) DEFAULT NULL,
    max_daily_cost NUMERIC(20,8) DEFAULT NULL,
    max_monthly_cost NUMERIC(20,8) DEFAULT NULL,
    require_verified_catalog BOOLEAN NOT NULL DEFAULT TRUE,
    allow_stale_catalog BOOLEAN NOT NULL DEFAULT FALSE,
    max_catalog_age_seconds BIGINT DEFAULT 43200,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CHECK (
        (scope = 'global' AND provider_key IS NULL AND vault_id IS NULL) OR
        (scope = 'provider' AND provider_key IS NOT NULL AND vault_id IS NULL) OR
        (scope = 'vault' AND vault_id IS NOT NULL)
    )
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_s3_price_budget_policy_global
    ON s3_price_budget_policy ((scope))
    WHERE scope = 'global';

CREATE UNIQUE INDEX IF NOT EXISTS ux_s3_price_budget_policy_provider
    ON s3_price_budget_policy (scope, provider_key)
    WHERE scope = 'provider';

CREATE UNIQUE INDEX IF NOT EXISTS ux_s3_price_budget_policy_vault
    ON s3_price_budget_policy (scope, vault_id, COALESCE(provider_key, ''))
    WHERE scope = 'vault';

DO $$ BEGIN
CREATE TRIGGER set_updated_at
    BEFORE UPDATE ON s3_price_budget_policy
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

CREATE TABLE IF NOT EXISTS s3_price_budget_ledger
(
    id SERIAL PRIMARY KEY,
    policy_id INTEGER NOT NULL REFERENCES s3_price_budget_policy(id) ON DELETE CASCADE,
    run_uuid TEXT NOT NULL,
    vault_id INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    provider_key TEXT NOT NULL,
    currency VARCHAR(8) NOT NULL DEFAULT 'USD',
    window_type VARCHAR NOT NULL CHECK (window_type IN ('per_run', 'daily', 'monthly')),
    window_start TIMESTAMP NOT NULL,
    window_end TIMESTAMP NOT NULL,
    reserved_cost NUMERIC(20,8) NOT NULL DEFAULT 0,
    committed_cost NUMERIC(20,8) DEFAULT NULL,
    status VARCHAR NOT NULL DEFAULT 'reserved' CHECK (status IN ('reserved', 'committed', 'released', 'expired')),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_policy_window_status
    ON s3_price_budget_ledger (policy_id, window_type, window_start, status);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_run_uuid
    ON s3_price_budget_ledger (run_uuid);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_vault
    ON s3_price_budget_ledger (vault_id);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_provider
    ON s3_price_budget_ledger (provider_key);

DO $$ BEGIN
CREATE TRIGGER set_updated_at
    BEFORE UPDATE ON s3_price_budget_ledger
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
