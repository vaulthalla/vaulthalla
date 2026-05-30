CREATE TABLE IF NOT EXISTS operator_notification
(
    id SERIAL PRIMARY KEY,
    type TEXT NOT NULL,
    severity TEXT NOT NULL CHECK (severity IN ('info', 'warning', 'error', 'critical')),
    title TEXT NOT NULL,
    message TEXT NOT NULL,
    scope TEXT DEFAULT NULL,
    vault_id INTEGER DEFAULT NULL REFERENCES vault(id) ON DELETE CASCADE,
    provider_key TEXT DEFAULT NULL,
    policy_id INTEGER DEFAULT NULL REFERENCES s3_price_budget_policy(id) ON DELETE SET NULL,
    run_uuid TEXT DEFAULT NULL,
    metadata JSONB DEFAULT NULL,
    acknowledged_at TIMESTAMP DEFAULT NULL,
    acknowledged_by INTEGER DEFAULT NULL REFERENCES users(id) ON DELETE SET NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_operator_notification_unacked
    ON operator_notification (acknowledged_at, severity, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_operator_notification_vault
    ON operator_notification (vault_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_operator_notification_policy
    ON operator_notification (policy_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_operator_notification_run
    ON operator_notification (run_uuid);

CREATE TABLE IF NOT EXISTS s3_price_budget_override
(
    id SERIAL PRIMARY KEY,
    run_uuid TEXT DEFAULT NULL,
    vault_id INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    requested_by INTEGER DEFAULT NULL REFERENCES users(id) ON DELETE SET NULL,
    approved_by INTEGER DEFAULT NULL REFERENCES users(id) ON DELETE SET NULL,
    status TEXT NOT NULL CHECK (status IN ('requested', 'approved', 'denied', 'expired', 'used', 'cancelled')),
    reason TEXT DEFAULT NULL,
    scope TEXT NOT NULL DEFAULT 'single_run',
    policy_ids JSONB DEFAULT NULL,
    estimated_cost NUMERIC(20,8) DEFAULT NULL,
    currency VARCHAR(8) DEFAULT 'USD',
    expires_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    decided_at TIMESTAMP DEFAULT NULL,
    used_at TIMESTAMP DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_override_vault_status
    ON s3_price_budget_override (vault_id, status, expires_at);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_override_run_uuid
    ON s3_price_budget_override (run_uuid);

CREATE TABLE IF NOT EXISTS s3_price_budget_alert_state
(
    id SERIAL PRIMARY KEY,
    policy_id INTEGER REFERENCES s3_price_budget_policy(id) ON DELETE CASCADE,
    alert_key TEXT NOT NULL,
    window_type TEXT NOT NULL,
    window_start TIMESTAMP NOT NULL,
    last_triggered_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    acknowledged_at TIMESTAMP DEFAULT NULL,
    UNIQUE(policy_id, alert_key, window_type, window_start)
);

CREATE INDEX IF NOT EXISTS idx_s3_price_budget_alert_state_policy
    ON s3_price_budget_alert_state (policy_id, window_type, window_start);
