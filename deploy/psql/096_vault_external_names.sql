-- Stable vault external names.
--
-- Existing vaults get:
--   * slug: S3-safe display-name slug, de-duped deterministically.
--   * fuse_name: the legacy FUSE-visible component lowercased with spaces
--     changed to underscores, de-duped with "-<id>" only when collisions make
--     preserving the exact legacy component impossible.
--
-- Existing s3_gateway_bucket.bucket_name rows are intentionally not rewritten.

CREATE OR REPLACE FUNCTION vh_is_s3_safe_name(value TEXT)
RETURNS BOOLEAN
LANGUAGE SQL
IMMUTABLE
AS $$
    SELECT value ~ '^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$'
$$;

CREATE OR REPLACE FUNCTION vh_is_safe_fuse_name(value TEXT)
RETURNS BOOLEAN
LANGUAGE SQL
IMMUTABLE
AS $$
    SELECT value IS NOT NULL
       AND length(value) > 0
       AND value NOT IN ('.', '..')
       AND position('/' IN value) = 0
       AND position(chr(92) IN value) = 0
$$;

CREATE OR REPLACE FUNCTION vh_slugify(value TEXT)
RETURNS TEXT
LANGUAGE plpgsql
IMMUTABLE
AS $$
DECLARE
    out TEXT;
BEGIN
    out := lower(coalesce(value, ''));
    out := regexp_replace(out, '[^a-z0-9]+', '-', 'g');
    out := regexp_replace(out, '-+', '-', 'g');
    out := regexp_replace(out, '(^-+|-+$)', '', 'g');

    IF out = '' THEN
        out := 'vault';
    END IF;

    IF length(out) > 63 THEN
        out := substring(out FROM 1 FOR 63);
        out := regexp_replace(out, '-+$', '', 'g');
    END IF;

    IF length(out) < 3 THEN
        out := rpad(out, 3, '0');
    END IF;

    RETURN out;
END;
$$;

CREATE OR REPLACE FUNCTION vh_legacy_fuse_name(value TEXT)
RETURNS TEXT
LANGUAGE SQL
IMMUTABLE
AS $$
    SELECT lower(replace(coalesce(value, ''), ' ', '_'))
$$;

CREATE OR REPLACE FUNCTION vh_name_with_id_suffix(base TEXT, vault_id INTEGER, max_len INTEGER)
RETURNS TEXT
LANGUAGE plpgsql
IMMUTABLE
AS $$
DECLARE
    suffix TEXT;
    head TEXT;
BEGIN
    suffix := '-' || coalesce(vault_id, 0)::TEXT;
    head := coalesce(nullif(base, ''), 'vault');

    IF length(head) + length(suffix) > max_len THEN
        head := substring(head FROM 1 FOR greatest(1, max_len - length(suffix)));
        head := regexp_replace(head, '-+$', '', 'g');
        IF head = '' THEN
            head := 'vault';
        END IF;
    END IF;

    RETURN substring(head || suffix FROM 1 FOR max_len);
END;
$$;

CREATE OR REPLACE FUNCTION vh_unique_vault_slug(base TEXT, vault_id INTEGER)
RETURNS TEXT
LANGUAGE plpgsql
AS $$
DECLARE
    candidate TEXT;
    attempt INTEGER := 1;
BEGIN
    candidate := vh_slugify(base);

    WHILE EXISTS (
        SELECT 1 FROM vault
        WHERE id <> coalesce(vault_id, 0)
          AND slug = candidate
    ) LOOP
        IF attempt = 1 THEN
            candidate := vh_name_with_id_suffix(vh_slugify(base), vault_id, 63);
        ELSE
            candidate := vh_name_with_id_suffix(vh_slugify(base) || '-' || attempt::TEXT, vault_id, 63);
        END IF;
        attempt := attempt + 1;
    END LOOP;

    RETURN candidate;
END;
$$;

CREATE OR REPLACE FUNCTION vh_unique_vault_fuse_name(base TEXT, vault_id INTEGER)
RETURNS TEXT
LANGUAGE plpgsql
AS $$
DECLARE
    candidate TEXT;
    attempt INTEGER := 1;
BEGIN
    candidate := coalesce(nullif(base, ''), 'vault');

    WHILE EXISTS (
        SELECT 1 FROM vault
        WHERE id <> coalesce(vault_id, 0)
          AND coalesce(fuse_name, slug) = candidate
    ) LOOP
        IF attempt = 1 THEN
            candidate := vh_name_with_id_suffix(base, vault_id, 255);
        ELSE
            candidate := vh_name_with_id_suffix(base || '-' || attempt::TEXT, vault_id, 255);
        END IF;
        attempt := attempt + 1;
    END LOOP;

    RETURN candidate;
END;
$$;

ALTER TABLE vault
    ADD COLUMN IF NOT EXISTS slug TEXT,
    ADD COLUMN IF NOT EXISTS fuse_name TEXT;

ALTER TABLE vault DROP CONSTRAINT IF EXISTS vault_name_key;
ALTER TABLE vault DROP CONSTRAINT IF EXISTS vault_name_owner_id_key;

DO $$
DECLARE
    rec RECORD;
    base TEXT;
    candidate TEXT;
    attempt INTEGER;
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS _vh_used_vault_slug (name TEXT PRIMARY KEY) ON COMMIT DROP;
    TRUNCATE _vh_used_vault_slug;

    FOR rec IN SELECT id, name, slug FROM vault ORDER BY id LOOP
        base := coalesce(nullif(rec.slug, ''), vh_slugify(rec.name));
        IF NOT vh_is_s3_safe_name(base) THEN
            base := vh_slugify(rec.name);
        END IF;

        candidate := base;
        attempt := 1;
        WHILE EXISTS (SELECT 1 FROM _vh_used_vault_slug WHERE name = candidate) LOOP
            IF attempt = 1 THEN
                candidate := vh_name_with_id_suffix(base, rec.id, 63);
            ELSE
                candidate := vh_name_with_id_suffix(base || '-' || attempt::TEXT, rec.id, 63);
            END IF;
            attempt := attempt + 1;
        END LOOP;

        UPDATE vault SET slug = candidate WHERE id = rec.id;
        INSERT INTO _vh_used_vault_slug (name) VALUES (candidate);
    END LOOP;
END $$;

DO $$
DECLARE
    rec RECORD;
    base TEXT;
    candidate TEXT;
    attempt INTEGER;
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS _vh_used_vault_fuse_name (name TEXT PRIMARY KEY) ON COMMIT DROP;
    TRUNCATE _vh_used_vault_fuse_name;

    FOR rec IN SELECT id, name, slug, fuse_name FROM vault ORDER BY id LOOP
        base := coalesce(nullif(rec.fuse_name, ''), vh_legacy_fuse_name(rec.name));
        IF NOT vh_is_safe_fuse_name(base) THEN
            base := rec.slug;
        END IF;

        candidate := base;
        attempt := 1;
        WHILE EXISTS (SELECT 1 FROM _vh_used_vault_fuse_name WHERE name = candidate) LOOP
            IF attempt = 1 THEN
                candidate := vh_name_with_id_suffix(base, rec.id, 255);
            ELSE
                candidate := vh_name_with_id_suffix(base || '-' || attempt::TEXT, rec.id, 255);
            END IF;
            attempt := attempt + 1;
        END LOOP;

        UPDATE vault SET fuse_name = candidate WHERE id = rec.id;
        INSERT INTO _vh_used_vault_fuse_name (name) VALUES (candidate);
    END LOOP;
END $$;

ALTER TABLE vault
    ALTER COLUMN slug SET NOT NULL;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'vault_slug_s3_safe') THEN
        ALTER TABLE vault
            ADD CONSTRAINT vault_slug_s3_safe
            CHECK (vh_is_s3_safe_name(slug));
    END IF;

    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'vault_fuse_name_safe') THEN
        ALTER TABLE vault
            ADD CONSTRAINT vault_fuse_name_safe
            CHECK (fuse_name IS NULL OR vh_is_safe_fuse_name(fuse_name));
    END IF;

    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'vault_slug_unique') THEN
        ALTER TABLE vault
            ADD CONSTRAINT vault_slug_unique UNIQUE (slug);
    END IF;

    IF to_regclass('idx_vault_effective_fuse_name_unique') IS NULL THEN
        CREATE UNIQUE INDEX idx_vault_effective_fuse_name_unique
            ON vault ((coalesce(fuse_name, slug)));
    END IF;

    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 's3_gateway_bucket_name_s3_safe') THEN
        ALTER TABLE s3_gateway_bucket
            ADD CONSTRAINT s3_gateway_bucket_name_s3_safe
            CHECK (vh_is_s3_safe_name(bucket_name)) NOT VALID;
    END IF;
END $$;

CREATE OR REPLACE FUNCTION vh_vault_external_names_before_write()
RETURNS TRIGGER
LANGUAGE plpgsql
AS $$
DECLARE
    effective_fuse_name TEXT;
BEGIN
    IF NEW.slug IS NULL OR NEW.slug = '' THEN
        NEW.slug := vh_unique_vault_slug(vh_slugify(NEW.name), NEW.id);
    END IF;

    IF NOT vh_is_s3_safe_name(NEW.slug) THEN
        RAISE EXCEPTION 'Invalid vault slug: %. Use 3-63 lowercase letters, digits, or hyphens, beginning and ending with a letter or digit.', NEW.slug
            USING ERRCODE = '23514';
    END IF;

    IF NEW.fuse_name IS NOT NULL AND NOT vh_is_safe_fuse_name(NEW.fuse_name) THEN
        RAISE EXCEPTION 'Invalid vault fuse_name: %. It must be a safe single path component.', NEW.fuse_name
            USING ERRCODE = '23514';
    END IF;

    IF EXISTS (SELECT 1 FROM vault WHERE id <> coalesce(NEW.id, 0) AND slug = NEW.slug) THEN
        RAISE EXCEPTION 'Duplicate vault slug: %', NEW.slug
            USING ERRCODE = '23505';
    END IF;

    effective_fuse_name := coalesce(NEW.fuse_name, NEW.slug);
    IF EXISTS (
        SELECT 1 FROM vault
        WHERE id <> coalesce(NEW.id, 0)
          AND coalesce(fuse_name, slug) = effective_fuse_name
    ) THEN
        RAISE EXCEPTION 'Duplicate effective FUSE name: %', effective_fuse_name
            USING ERRCODE = '23505';
    END IF;

    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_vault_external_names_before_write ON vault;
CREATE TRIGGER trg_vault_external_names_before_write
    BEFORE INSERT OR UPDATE OF name, slug, fuse_name ON vault
    FOR EACH ROW
    EXECUTE FUNCTION vh_vault_external_names_before_write();
