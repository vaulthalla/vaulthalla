-- #############################################
-- Symlinks & Protected System Principal Guards
-- #############################################

-- Symlink metadata. Targets are vault-relative policy data, not host paths.
CREATE TABLE IF NOT EXISTS symlinks
(
    fs_entry_id INTEGER PRIMARY KEY REFERENCES fs_entry (id) ON DELETE CASCADE,
    target      TEXT NOT NULL CHECK (length(target) > 0 AND target NOT LIKE '/%')
);

CREATE INDEX IF NOT EXISTS idx_symlinks_target_entry
    ON symlinks (fs_entry_id);

-- Protected identity flags. Normal write paths intentionally do not set these.
ALTER TABLE users
    ADD COLUMN IF NOT EXISTS protected BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS system_only BOOLEAN NOT NULL DEFAULT FALSE;

CREATE OR REPLACE FUNCTION vaulthalla_bootstrap_enabled()
RETURNS BOOLEAN AS $$
BEGIN
    RETURN COALESCE(current_setting('vaulthalla.bootstrap', TRUE), '') = 'on';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION guard_protected_user_insert()
RETURNS TRIGGER AS $$
BEGIN
    IF vaulthalla_bootstrap_enabled() THEN
        RETURN NEW;
    END IF;

    IF NEW.protected OR NEW.system_only THEN
        RAISE EXCEPTION 'protected/system_only flags are bootstrap-only';
    END IF;

    IF NEW.linux_uid = 0 THEN
        RAISE EXCEPTION 'linux_uid 0 is reserved for the root principal';
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION guard_protected_user_update()
RETURNS TRIGGER AS $$
BEGIN
    IF vaulthalla_bootstrap_enabled() THEN
        RETURN NEW;
    END IF;

    IF NEW.protected IS DISTINCT FROM OLD.protected
       OR NEW.system_only IS DISTINCT FROM OLD.system_only THEN
        RAISE EXCEPTION 'protected/system_only flags are bootstrap-only';
    END IF;

    IF OLD.protected THEN
        IF NEW.name IS DISTINCT FROM OLD.name THEN
            RAISE EXCEPTION 'protected users cannot be renamed';
        END IF;

        IF NEW.linux_uid IS DISTINCT FROM OLD.linux_uid THEN
            RAISE EXCEPTION 'protected users cannot have linux_uid changed';
        END IF;

        IF NEW.is_active IS DISTINCT FROM OLD.is_active THEN
            RAISE EXCEPTION 'protected users cannot be activated or deactivated through normal paths';
        END IF;
    END IF;

    IF NEW.linux_uid = 0 AND NEW.name <> 'root' THEN
        RAISE EXCEPTION 'linux_uid 0 is reserved for the root principal';
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION guard_protected_user_delete()
RETURNS TRIGGER AS $$
BEGIN
    IF vaulthalla_bootstrap_enabled() THEN
        RETURN OLD;
    END IF;

    IF OLD.protected THEN
        RAISE EXCEPTION 'protected users cannot be deleted';
    END IF;

    RETURN OLD;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION guard_protected_admin_role_assignment()
RETURNS TRIGGER AS $$
DECLARE
    target_user_id INTEGER;
    old_protected BOOLEAN;
    new_protected BOOLEAN;
BEGIN
    IF vaulthalla_bootstrap_enabled() THEN
        IF TG_OP = 'DELETE' THEN
            RETURN OLD;
        END IF;
        RETURN NEW;
    END IF;

    IF TG_OP = 'DELETE' THEN
        target_user_id := OLD.user_id;
        SELECT u.protected INTO old_protected FROM users u WHERE u.id = target_user_id;
        IF COALESCE(old_protected, FALSE) THEN
            RAISE EXCEPTION 'protected users cannot be demoted';
        END IF;
        RETURN OLD;
    END IF;

    IF TG_OP = 'UPDATE' THEN
        SELECT u.protected INTO old_protected FROM users u WHERE u.id = OLD.user_id;
        SELECT u.protected INTO new_protected FROM users u WHERE u.id = NEW.user_id;
        IF COALESCE(old_protected, FALSE) OR COALESCE(new_protected, FALSE) THEN
            RAISE EXCEPTION 'protected users cannot be demoted';
        END IF;
        RETURN NEW;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_guard_protected_user_insert ON users;
CREATE TRIGGER trg_guard_protected_user_insert
    BEFORE INSERT ON users
    FOR EACH ROW
    EXECUTE FUNCTION guard_protected_user_insert();

DROP TRIGGER IF EXISTS trg_guard_protected_user_update ON users;
CREATE TRIGGER trg_guard_protected_user_update
    BEFORE UPDATE ON users
    FOR EACH ROW
    EXECUTE FUNCTION guard_protected_user_update();

DROP TRIGGER IF EXISTS trg_guard_protected_user_delete ON users;
CREATE TRIGGER trg_guard_protected_user_delete
    BEFORE DELETE ON users
    FOR EACH ROW
    EXECUTE FUNCTION guard_protected_user_delete();

DROP TRIGGER IF EXISTS trg_guard_protected_admin_role_assignment_update ON admin_role_assignments;
CREATE TRIGGER trg_guard_protected_admin_role_assignment_update
    BEFORE UPDATE ON admin_role_assignments
    FOR EACH ROW
    EXECUTE FUNCTION guard_protected_admin_role_assignment();

DROP TRIGGER IF EXISTS trg_guard_protected_admin_role_assignment_delete ON admin_role_assignments;
CREATE TRIGGER trg_guard_protected_admin_role_assignment_delete
    BEFORE DELETE ON admin_role_assignments
    FOR EACH ROW
    EXECUTE FUNCTION guard_protected_admin_role_assignment();
