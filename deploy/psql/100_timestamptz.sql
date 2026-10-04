-- Every `timestamp` (without time zone) column becomes `timestamptz` (#157).
--
-- Naive columns stored the writing session's local wall time: on a server whose TimeZone isn't UTC,
-- DEFAULT CURRENT_TIMESTAMP / now() landed as local time, while the daemon parses naive values as UTC and prints
-- them with a 'Z', so times were off by the UTC offset (and disagreed with the timestamptz columns of 070-098).
-- From this release every daemon session runs with TimeZone=UTC (db::Connection::configureSession), and this
-- migration makes the stored values absolute.
--
-- Source zone: rows written before this migration are interpreted in the zone the daemon's sessions used before
-- they were forced to UTC, i.e. the database's configured TimeZone (ALTER DATABASE/ROLE ... SET timezone, else
-- postgresql.conf). The daemon records it per session in the custom setting vaulthalla.database_timezone; a manual
-- psql run falls back to that session's TimeZone. ASSUMPTION: every historical row was written in that zone. Rows
-- written while the server used a different zone (the zone was changed after install, or a client with its own
-- PGTZ wrote them) keep that difference; nothing in the data records which zone wrote a row. Values in the hour a
-- DST change repeats resolve to one of the two instants (PostgreSQL's rule for ambiguous local times).
--
-- Idempotent and safe on upgraded installs: columns are found by catalog lookup in the current schema and only
-- `timestamp without time zone` columns are converted; already-timestamptz columns are untouched, so a re-run is
-- a no-op. Defaults are dropped and restored around the type change (PostgreSQL's documented pattern), and
-- indexes/constraints on the columns are rebuilt by ALTER TABLE. A column a view or rule depends on (the schema
-- defines none; an operator might) is skipped with a WARNING instead of failing the upgrade; the daemon still reads
-- it correctly as UTC from now on. Converting from a non-UTC zone rewrites each affected table once, inside the
-- migration transaction; from UTC no USING clause is needed and PostgreSQL skips the rewrite.

DO $$
DECLARE
    source_zone TEXT := COALESCE(NULLIF(current_setting('vaulthalla.database_timezone', true), ''),
                                 current_setting('TimeZone'));
    utc_names CONSTANT TEXT[] := ARRAY['utc', 'etc/utc', 'gmt', 'etc/gmt', 'uct', 'etc/uct', 'zulu', 'etc/zulu',
                                       'universal', 'etc/universal', 'gmt0', 'etc/gmt0', 'gmt+0', 'gmt-0',
                                       'etc/gmt+0', 'etc/gmt-0', 'greenwich', 'etc/greenwich'];
    no_rewrite BOOLEAN;
    tbl RECORD;
    col RECORD;
    drops TEXT;
    alters TEXT;
    defaults TEXT;
BEGIN
    -- Fail with the zone's name rather than half-way through if the recorded zone is unusable.
    PERFORM TIMESTAMP '2000-01-01 00:00:00' AT TIME ZONE source_zone;

    -- The implicit cast reads naive values in the session zone: only correct (and rewrite-free) when both the
    -- source and this session are UTC.
    no_rewrite := lower(source_zone) = ANY (utc_names) AND lower(current_setting('TimeZone')) = ANY (utc_names);

    RAISE NOTICE 'converting timestamp columns to timestamptz, reading existing values as %', source_zone;

    FOR tbl IN
        SELECT DISTINCT c.oid AS relid, c.relname
        FROM pg_class c
        JOIN pg_attribute a ON a.attrelid = c.oid
        WHERE c.relnamespace = current_schema()::regnamespace
          AND c.relkind IN ('r', 'p')
          AND a.attnum > 0
          AND NOT a.attisdropped
          AND a.atttypid = 'timestamp without time zone'::regtype
        ORDER BY c.relname
    LOOP
        drops := '';
        alters := '';
        defaults := '';
        FOR col IN
            SELECT a.attname, pg_get_expr(d.adbin, d.adrelid) AS default_expr
            FROM pg_attribute a
            LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
            WHERE a.attrelid = tbl.relid
              AND a.attnum > 0
              AND NOT a.attisdropped
              AND a.atttypid = 'timestamp without time zone'::regtype
            ORDER BY a.attnum
        LOOP
            IF EXISTS (
                SELECT 1
                FROM pg_depend dep
                JOIN pg_rewrite r ON r.oid = dep.objid
                WHERE dep.classid = 'pg_rewrite'::regclass
                  AND dep.refclassid = 'pg_class'::regclass
                  AND dep.refobjid = tbl.relid
                  AND dep.refobjsubid = (SELECT attnum FROM pg_attribute
                                         WHERE attrelid = tbl.relid AND attname = col.attname)
                  AND r.ev_class <> tbl.relid
            ) THEN
                RAISE WARNING 'skipping %.%: a view or rule depends on it (it stays timestamp without time zone)',
                    tbl.relname, col.attname;
                CONTINUE;
            END IF;

            IF col.default_expr IS NOT NULL THEN
                drops := drops || format('ALTER COLUMN %I DROP DEFAULT, ', col.attname);
                defaults := defaults || format(', ALTER COLUMN %I SET DEFAULT %s', col.attname, col.default_expr);
            END IF;
            IF no_rewrite THEN
                alters := alters || format(', ALTER COLUMN %I TYPE timestamptz', col.attname);
            ELSE
                alters := alters || format(', ALTER COLUMN %I TYPE timestamptz USING %I AT TIME ZONE %L',
                                           col.attname, col.attname, source_zone);
            END IF;
        END LOOP;

        CONTINUE WHEN alters = '';
        -- One statement per table: drop defaults, change every column's type, restore defaults.
        EXECUTE format('ALTER TABLE %s %s%s%s', tbl.relid::regclass, drops, substr(alters, 3), defaults);
    END LOOP;
END
$$;
