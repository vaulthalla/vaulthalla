-- When each account's password was last set (#163).
--
-- The daemon reported password_changed_at, but no column held it, so it was null after every reload. The column
-- is stamped by a trigger whenever password_hash is written with a different value (and on insert with a
-- password), so every path records it: web self-service change, admin reset, the CLI, `vh setup
-- set-super-admin-password`, the bootstrap credential and the seed.
--
-- Idempotent and safe on upgraded installs: the column is added only if missing and stays NULL for existing rows
-- (when their password was set is unknown), and the function and trigger are replaced in place.

ALTER TABLE users ADD COLUMN IF NOT EXISTS password_changed_at TIMESTAMPTZ;

CREATE OR REPLACE FUNCTION stamp_user_password_changed_at()
RETURNS TRIGGER AS $$
BEGIN
    IF TG_OP = 'INSERT' THEN
        IF NEW.password_changed_at IS NULL AND COALESCE(NEW.password_hash, '') <> '' THEN
            NEW.password_changed_at := NOW();
        END IF;
    ELSIF NEW.password_hash IS DISTINCT FROM OLD.password_hash THEN
        NEW.password_changed_at := NOW();
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_stamp_user_password_changed_at ON users;
CREATE TRIGGER trg_stamp_user_password_changed_at
    BEFORE INSERT OR UPDATE OF password_hash ON users
    FOR EACH ROW
    EXECUTE FUNCTION stamp_user_password_changed_at();
