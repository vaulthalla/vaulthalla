-- Deleting a user must not be blocked by rows that only record who did something (#179).
--
-- 040_files.sql and 070_link_shares.sql declared their references to users(id) with no delete action, so
-- `vh user delete` failed with a foreign key violation for anyone who had uploaded to a vault they don't own, kept a
-- file version, held a lock, or created, used or was named by a share link (share_access_event.actor_user_id).
--
-- Attribution and audit columns become ON DELETE SET NULL: the row keeps its event, it loses the identity link.
-- Rows that only make sense while their user exists are removed with the user:
--   file_locks.locked_by   a lock held by a deleted account
--   share_link.created_by  public links a deleted account created; the account that granted them is gone, so the
--                          links are revoked with it (their sessions, uploads and challenges cascade from
--                          share_link, and share_access_event keeps the history with share_id set to NULL).
--
-- Idempotent and safe on upgraded installs: each foreign key is found by catalog lookup (whatever it is named) and
-- replaced only when its delete action differs from the target. Tables a later migration dropped are skipped.
-- Existing rows already satisfy the references (the old constraints enforced them), so validation cannot fail.

DO $$
DECLARE
    spec RECORD;
    fk RECORD;
    target_type "char";
    action_sql TEXT;
BEGIN
    FOR spec IN
        SELECT *
        FROM (VALUES
            ('fs_entry',           'created_by',       'n'),
            ('fs_entry',           'last_modified_by', 'n'),
            ('files_trashed',      'trashed_by',       'n'),
            ('operations',         'executed_by',      'n'),
            ('file_versions',      'created_by',       'n'),
            ('file_locks',         'locked_by',        'c'),
            ('audit_log',          'user_id',          'n'),
            ('file_activity',      'user_id',          'n'),
            ('share_link',         'created_by',       'c'),
            ('share_link',         'updated_by',       'n'),
            ('share_link',         'revoked_by',       'n'),
            ('share_access_event', 'actor_user_id',    'n')
        ) AS t(table_name, column_name, delete_action)
    LOOP
        IF to_regclass(spec.table_name) IS NULL THEN
            CONTINUE;
        END IF;

        target_type := spec.delete_action::"char";
        action_sql := CASE spec.delete_action WHEN 'c' THEN 'CASCADE' ELSE 'SET NULL' END;

        FOR fk IN
            SELECT c.conname
            FROM pg_constraint c
            JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
            WHERE c.contype = 'f'
              AND c.conrelid = to_regclass(spec.table_name)
              AND c.confrelid = 'users'::regclass
              AND cardinality(c.conkey) = 1
              AND a.attname = spec.column_name
              AND c.confdeltype <> target_type
        LOOP
            EXECUTE format('ALTER TABLE %I DROP CONSTRAINT %I', spec.table_name, fk.conname);
        END LOOP;

        IF NOT EXISTS (
            SELECT 1
            FROM pg_constraint c
            JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
            WHERE c.contype = 'f'
              AND c.conrelid = to_regclass(spec.table_name)
              AND c.confrelid = 'users'::regclass
              AND cardinality(c.conkey) = 1
              AND a.attname = spec.column_name
        ) THEN
            EXECUTE format(
                'ALTER TABLE %I ADD CONSTRAINT %I FOREIGN KEY (%I) REFERENCES users (id) ON DELETE %s',
                spec.table_name,
                spec.table_name || '_' || spec.column_name || '_fkey',
                spec.column_name,
                action_sql
            );
        END IF;
    END LOOP;
END
$$;
