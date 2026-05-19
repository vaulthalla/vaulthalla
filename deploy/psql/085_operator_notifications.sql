CREATE TABLE IF NOT EXISTS operator_notification_delivery (
    id BIGSERIAL PRIMARY KEY,
    event_key TEXT NOT NULL,
    event_type TEXT NOT NULL,
    severity TEXT NOT NULL,
    provider TEXT NOT NULL,
    subject TEXT NOT NULL,
    recipient_group TEXT,
    recipient_count INTEGER NOT NULL DEFAULT 0,
    provider_message_id TEXT,
    status TEXT NOT NULL CHECK (status IN ('queued', 'sent', 'failed', 'suppressed', 'dry_run')),
    error_summary TEXT,
    fingerprint TEXT NOT NULL,
    first_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    sent_at TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_operator_notification_delivery_event
    ON operator_notification_delivery (event_key, fingerprint, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_operator_notification_delivery_status
    ON operator_notification_delivery (status, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_operator_notification_delivery_created
    ON operator_notification_delivery (created_at DESC);
