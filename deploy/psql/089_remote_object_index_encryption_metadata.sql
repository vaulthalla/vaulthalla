-- Preserve encrypted remote object metadata in the local index so manifest-backed
-- download/index paths can recover IV/key-version without falling back to LIST.
ALTER TABLE remote_object_index
    ADD COLUMN IF NOT EXISTS content_hash TEXT DEFAULT NULL;

ALTER TABLE remote_object_index
    ADD COLUMN IF NOT EXISTS encrypted BOOLEAN DEFAULT NULL;

ALTER TABLE remote_object_index
    ADD COLUMN IF NOT EXISTS encryption_iv TEXT DEFAULT NULL;

ALTER TABLE remote_object_index
    ADD COLUMN IF NOT EXISTS encrypted_with_key_version INTEGER DEFAULT NULL;
