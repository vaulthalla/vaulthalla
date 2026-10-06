-- Encrypted derived-artifact cache (rich preview platform).
--
-- cache_index becomes the one index for every derived artifact (thumbnails, page renders, posters, GLB from STEP,
-- transcodes). Each artifact is identified by (file_id, kind, variant), is valid only for the source content
-- generation it was derived from (source_id = the file's IV, which changes on every content write) and the
-- generator version, and is sealed on disk with the vault key under a fresh IV (artifact_iv / artifact_key_version).
-- status 'failed' rows are a negative cache so a malformed file cannot trigger a retry storm.
--
-- files.encryption_format records the at-rest format of each vault file (1 = one AES-256-GCM message per file,
-- body || 16-byte tag, IV in files.encryption_iv). Every existing file is format 1.
--
-- Idempotent and safe on upgraded installs: columns are added only if missing; the type CHECK is replaced in place.
-- Legacy 'thumbnail' rows describe plaintext JPEGs; they are dropped here and the daemon deletes the plaintext files
-- at startup and regenerates encrypted thumbnails on demand.

ALTER TABLE files ADD COLUMN IF NOT EXISTS encryption_format SMALLINT NOT NULL DEFAULT 1;

DELETE FROM cache_index WHERE type IN ('thumbnail', 'file');

ALTER TABLE cache_index DROP CONSTRAINT IF EXISTS cache_index_type_check;
ALTER TABLE cache_index ADD CONSTRAINT cache_index_type_check CHECK (type IN ('thumbnail', 'file', 'derived'));

ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS kind TEXT;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS variant TEXT;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS source_id TEXT;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS generator_version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS artifact_iv TEXT;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS artifact_key_version INTEGER;
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS status VARCHAR(8) NOT NULL DEFAULT 'ready';
ALTER TABLE cache_index ADD COLUMN IF NOT EXISTS failure_reason TEXT;

ALTER TABLE cache_index DROP CONSTRAINT IF EXISTS cache_index_status_check;
ALTER TABLE cache_index ADD CONSTRAINT cache_index_status_check CHECK (status IN ('ready', 'failed'));

CREATE UNIQUE INDEX IF NOT EXISTS uq_cache_index_derived_identity
    ON cache_index (file_id, kind, variant) WHERE type = 'derived';

CREATE INDEX IF NOT EXISTS idx_cache_index_last_accessed
    ON cache_index (last_accessed);
