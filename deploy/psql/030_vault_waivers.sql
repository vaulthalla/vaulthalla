-- ##################################
-- Cloud Encryption Waivers Schema
-- ##################################

CREATE TABLE IF NOT EXISTS cloud_encryption_waivers
(
    id           SERIAL PRIMARY KEY,

    -- References to the vault, user, and API key involved in the waiver
    vault_id     INTEGER REFERENCES vault (id) ON DELETE SET NULL,
    user_id      INTEGER REFERENCES users (id) ON DELETE SET NULL,
    api_key_id   INTEGER REFERENCES api_keys (id) ON DELETE SET NULL,

    -- Ensure we capture the Vault and User details at the time of waiver
    vault_name   VARCHAR(100) NOT NULL,
    user_name    VARCHAR(100) NOT NULL,
    user_email   VARCHAR(255) DEFAULT NULL,
    linux_uid    INTEGER,

    -- Ensure we capture the API key details at the time of waiver
    bucket       TEXT NOT NULL,
    api_key_name VARCHAR(100) NOT NULL,
    api_provider VARCHAR(50) NOT NULL,
    api_access_key TEXT NOT NULL,
    api_key_region VARCHAR(20) NOT NULL,
    api_key_endpoint TEXT,

    -- Details of the encryption settings the user is waiving
    encrypt_upstream BOOLEAN NOT NULL,
    waiver_text      TEXT NOT NULL,
    waived_at        TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );

-- If user who signs waiver is not vault owner, capture who authorized it
CREATE TABLE IF NOT EXISTS waiver_owner_overrides
(
    waiver_id                 INTEGER PRIMARY KEY REFERENCES cloud_encryption_waivers (id) ON DELETE CASCADE,
    owner_id                  INTEGER REFERENCES users (id) ON DELETE SET NULL,
    owner_name                VARCHAR(100) NOT NULL,
    owner_email               VARCHAR(255) DEFAULT NULL,

    overriding_role_id          INTEGER NOT NULL,
    overriding_role_name        VARCHAR(50) NOT NULL,
    overriding_role_scope       permission_categories NOT NULL,
    overriding_role_sync_permissions BIT(32) NOT NULL,
    snapshot_at                 TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    UNIQUE (waiver_id)
    );

-- Capture each permission bit's function in the bitmask at the time of waiver
CREATE TABLE IF NOT EXISTS waiver_permission_snapshots
(
    id                       SERIAL PRIMARY KEY,
    waiver_id                INTEGER REFERENCES cloud_encryption_waivers (id) ON DELETE CASCADE,
    snapshot_at              TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    permission_id            INTEGER NOT NULL,
    permission_name          VARCHAR(50) NOT NULL,
    permission_category      permission_categories NOT NULL,
    permission_bit_position  INTEGER NOT NULL CHECK (permission_bit_position >= 0 AND permission_bit_position < 64),

    UNIQUE (waiver_id, permission_bit_position)
    );

-- ##################################
-- RLS, Policies, & Immutability Guards
-- ##################################

-- Revoke public DELETE access and force RLS
REVOKE DELETE ON cloud_encryption_waivers FROM PUBLIC;
ALTER TABLE cloud_encryption_waivers ENABLE ROW LEVEL SECURITY;
ALTER TABLE cloud_encryption_waivers FORCE ROW LEVEL SECURITY;

-- Drop existing policies if they exist (CREATE POLICY has no OR REPLACE)
DO $$ BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_policies
        WHERE policyname = 'allow_insert' AND tablename = 'cloud_encryption_waivers'
    ) THEN
        DROP POLICY allow_insert ON cloud_encryption_waivers;
END IF;

    IF EXISTS (
        SELECT 1 FROM pg_policies
        WHERE policyname = 'allow_select_returning' AND tablename = 'cloud_encryption_waivers'
    ) THEN
        DROP POLICY allow_select_returning ON cloud_encryption_waivers;
END IF;

    IF EXISTS (
        SELECT 1 FROM pg_policies
        WHERE policyname = 'no_delete' AND tablename = 'cloud_encryption_waivers'
    ) THEN
        DROP POLICY no_delete ON cloud_encryption_waivers;
END IF;
END $$;

-- Only create role-scoped policies if the role exists (prevents fresh DB boot failure)
DO $$ BEGIN
    IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'vaulthalla') THEN

        CREATE POLICY allow_insert ON cloud_encryption_waivers
            FOR INSERT TO vaulthalla
            WITH CHECK (true);

        CREATE POLICY allow_select_returning ON cloud_encryption_waivers
            FOR SELECT TO vaulthalla
                                                        USING (true);

CREATE POLICY no_delete ON cloud_encryption_waivers
            FOR DELETE
USING (false);

ELSE
        RAISE NOTICE 'Skipping RLS policies: role "vaulthalla" does not exist.';
END IF;
END $$;

-- Prevent deletes (even if someone manages perms / bypass expectations)
CREATE OR REPLACE FUNCTION prevent_delete()
RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    RAISE EXCEPTION 'Deletion is not permitted on table %.', TG_TABLE_NAME;
RETURN NULL;
END;
$$;

DO $$ BEGIN
CREATE TRIGGER prevent_delete_trigger
    BEFORE DELETE ON cloud_encryption_waivers
    FOR EACH ROW EXECUTE FUNCTION prevent_delete();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

-- Block updates to immutable waiver fields
CREATE OR REPLACE FUNCTION block_immutable_waiver_updates()
RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    IF NEW.vault_name IS DISTINCT FROM OLD.vault_name OR
       NEW.user_name IS DISTINCT FROM OLD.user_name OR
       NEW.user_email IS DISTINCT FROM OLD.user_email OR
       NEW.linux_uid IS DISTINCT FROM OLD.linux_uid OR
       NEW.bucket IS DISTINCT FROM OLD.bucket OR
       NEW.api_key_name IS DISTINCT FROM OLD.api_key_name OR
       NEW.api_provider IS DISTINCT FROM OLD.api_provider OR
       NEW.api_access_key IS DISTINCT FROM OLD.api_access_key OR
       NEW.api_key_region IS DISTINCT FROM OLD.api_key_region OR
       NEW.api_key_endpoint IS DISTINCT FROM OLD.api_key_endpoint OR
       NEW.encrypt_upstream IS DISTINCT FROM OLD.encrypt_upstream OR
       NEW.waiver_text IS DISTINCT FROM OLD.waiver_text OR
       NEW.waived_at IS DISTINCT FROM OLD.waived_at
    THEN
        RAISE EXCEPTION 'Immutable waiver fields cannot be updated.';
END IF;

RETURN NEW;
END;
$$;

DO $$ BEGIN
CREATE TRIGGER block_immutable_waiver_updates_trigger
    BEFORE UPDATE ON cloud_encryption_waivers
    FOR EACH ROW EXECUTE FUNCTION block_immutable_waiver_updates();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
