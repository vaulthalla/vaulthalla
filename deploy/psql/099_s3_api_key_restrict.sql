-- An S3 vault's binding must never disappear because its upstream API key was removed.
--
-- s3.api_key_id was ON DELETE CASCADE (020_vaults.sql): deleting a key (or the account owning it) silently deleted
-- every dependent vault's s3 row (bucket, encrypt_upstream). The web console's key edit was delete + re-create, so
-- saving an edit dropped those bindings too. ops::api_keys::remove and ops::users::remove now refuse while a vault
-- uses the key; this makes the database refuse as well, for anything racing those checks.
--
-- Idempotent and safe on upgraded installs: finds the existing single-column foreign key on s3.api_key_id by
-- catalog lookup (whatever it is named), drops it only when its delete action is not already RESTRICT, and adds
-- the RESTRICT constraint only when none exists. Existing rows already satisfy the reference (the old constraint
-- enforced it), so the validation scan cannot fail.

DO $$
DECLARE
    fk RECORD;
BEGIN
    FOR fk IN
        SELECT c.conname, c.confdeltype
        FROM pg_constraint c
        JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
        WHERE c.contype = 'f'
          AND c.conrelid = 's3'::regclass
          AND c.confrelid = 'api_keys'::regclass
          AND cardinality(c.conkey) = 1
          AND a.attname = 'api_key_id'
    LOOP
        IF fk.confdeltype <> 'r' THEN
            EXECUTE format('ALTER TABLE s3 DROP CONSTRAINT %I', fk.conname);
        END IF;
    END LOOP;

    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint c
        JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
        WHERE c.contype = 'f'
          AND c.conrelid = 's3'::regclass
          AND c.confrelid = 'api_keys'::regclass
          AND cardinality(c.conkey) = 1
          AND a.attname = 'api_key_id'
          AND c.confdeltype = 'r'
    ) THEN
        ALTER TABLE s3
            ADD CONSTRAINT s3_api_key_id_fkey FOREIGN KEY (api_key_id) REFERENCES api_keys (id) ON DELETE RESTRICT;
    END IF;
END
$$;
