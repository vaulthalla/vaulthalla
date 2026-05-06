CREATE TABLE IF NOT EXISTS dashboard_preferences (
    id SERIAL PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    preference_key VARCHAR(64) NOT NULL DEFAULT 'dashboard.home',
    layout_json JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT dashboard_preferences_preference_key_not_empty CHECK (length(trim(preference_key)) > 0),
    CONSTRAINT dashboard_preferences_layout_object CHECK (jsonb_typeof(layout_json) = 'object'),
    CONSTRAINT dashboard_preferences_user_key_unique UNIQUE (user_id, preference_key)
);

CREATE INDEX IF NOT EXISTS idx_dashboard_preferences_user_id
    ON dashboard_preferences (user_id);

DO $$ BEGIN
CREATE TRIGGER set_dashboard_preferences_updated_at
    BEFORE UPDATE ON dashboard_preferences
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
