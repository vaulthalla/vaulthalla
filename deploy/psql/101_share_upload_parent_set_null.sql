-- A folder that once received a share-link upload could never be deleted.
--
-- share_upload.target_parent_entry_id was NOT NULL REFERENCES fs_entry (id) with no delete action (070_link_shares.sql),
-- while every other share column that points at fs_entry cascades or sets null. Deleting the folder (or anything
-- above it) failed with a raw foreign key violation that the web console showed verbatim. share_upload rows are the
-- upload history of a link, so they outlive the folder: the column becomes nullable and the reference
-- ON DELETE SET NULL, like created_entry_id next to it.
--
-- Idempotent and safe on upgraded installs: drops NOT NULL only if set, finds the existing single-column foreign key
-- by catalog lookup (whatever it is named), replaces it only when its delete action is not already SET NULL, and
-- adds the SET NULL constraint only when none exists. Existing rows already satisfy the reference.

ALTER TABLE share_upload ALTER COLUMN target_parent_entry_id DROP NOT NULL;

DO $$
DECLARE
    fk RECORD;
BEGIN
    FOR fk IN
        SELECT c.conname, c.confdeltype
        FROM pg_constraint c
        JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
        WHERE c.contype = 'f'
          AND c.conrelid = 'share_upload'::regclass
          AND c.confrelid = 'fs_entry'::regclass
          AND cardinality(c.conkey) = 1
          AND a.attname = 'target_parent_entry_id'
    LOOP
        IF fk.confdeltype <> 'n' THEN
            EXECUTE format('ALTER TABLE share_upload DROP CONSTRAINT %I', fk.conname);
        END IF;
    END LOOP;

    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint c
        JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = c.conkey[1]
        WHERE c.contype = 'f'
          AND c.conrelid = 'share_upload'::regclass
          AND c.confrelid = 'fs_entry'::regclass
          AND cardinality(c.conkey) = 1
          AND a.attname = 'target_parent_entry_id'
    ) THEN
        ALTER TABLE share_upload
            ADD CONSTRAINT share_upload_target_parent_entry_id_fkey
            FOREIGN KEY (target_parent_entry_id) REFERENCES fs_entry (id) ON DELETE SET NULL;
    END IF;
END $$;
