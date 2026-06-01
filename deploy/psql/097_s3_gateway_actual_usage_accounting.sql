ALTER TABLE s3_gateway_credentials
    ADD COLUMN IF NOT EXISTS enforce_budget_for_local_requests BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE s3_price_budget_ledger
    ADD COLUMN IF NOT EXISTS usage_source TEXT DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS synthetic BOOLEAN NOT NULL DEFAULT FALSE;

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_ledger_synthetic_gateway
    ON s3_price_budget_ledger(gateway_credential_id, synthetic, status);

CREATE TABLE IF NOT EXISTS s3_gateway_sync_origin
(
    id SERIAL PRIMARY KEY,
    vault_id INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key TEXT NOT NULL,
    operation TEXT NOT NULL,
    gateway_credential_id INTEGER REFERENCES s3_gateway_credentials(id) ON DELETE SET NULL,
    request_uuid TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    consumed_at TIMESTAMP DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_sync_origin_vault_object
    ON s3_gateway_sync_origin(vault_id, object_key, consumed_at);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_sync_origin_request_uuid
    ON s3_gateway_sync_origin(request_uuid);
