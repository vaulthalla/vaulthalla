-- ##################################
-- Filesystem & File Metadata Schema
-- ##################################

-- Core filesystem entries
CREATE TABLE IF NOT EXISTS fs_entry
(
    id               SERIAL PRIMARY KEY,
    vault_id         INTEGER REFERENCES vault (id) ON DELETE CASCADE DEFAULT NULL,
    parent_id        INTEGER REFERENCES fs_entry (id) ON DELETE CASCADE DEFAULT NULL,
    name             VARCHAR(500) NOT NULL,
    base32_alias     CHAR(33) UNIQUE,
    inode            BIGINT UNIQUE,
    created_by       INTEGER REFERENCES users (id),
    created_at       TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at       TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_modified_by INTEGER REFERENCES users (id),
    path             TEXT NOT NULL,
    mode             INTEGER DEFAULT 0755,
    owner_uid        INTEGER,
    group_gid        INTEGER,
    is_hidden        BOOLEAN DEFAULT FALSE,
    is_system        BOOLEAN DEFAULT FALSE,
    UNIQUE (parent_id, name)
    );

-- Directory metadata
CREATE TABLE IF NOT EXISTS directories
(
    fs_entry_id        INTEGER PRIMARY KEY REFERENCES fs_entry (id) ON DELETE CASCADE,
    file_count         INTEGER DEFAULT 0,
    subdirectory_count INTEGER DEFAULT 0,
    size_bytes         BIGINT DEFAULT 0
    );

-- File metadata
CREATE TABLE IF NOT EXISTS files
(
    fs_entry_id                INTEGER PRIMARY KEY REFERENCES fs_entry (id) ON DELETE CASCADE,
    size_bytes                 BIGINT DEFAULT 0,
    mime_type                  VARCHAR(255),
    content_hash               VARCHAR(128),
    encryption_iv              TEXT,
    encrypted_with_key_version INTEGER DEFAULT 1
    );

-- Trashed files
CREATE TABLE IF NOT EXISTS files_trashed
(
    id           SERIAL PRIMARY KEY,
    vault_id     INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    path         TEXT NOT NULL,
    backing_path TEXT NOT NULL,
    base32_alias CHAR(33) NOT NULL,
    size_bytes   BIGINT DEFAULT 0 NOT NULL,
    trashed_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    trashed_by   INTEGER REFERENCES users (id),
    deleted_at   TIMESTAMP
    );

-- Cache index
CREATE TABLE IF NOT EXISTS cache_index
(
    id            SERIAL PRIMARY KEY,
    vault_id      INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    file_id       INTEGER NOT NULL REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    path          TEXT NOT NULL,
    type          VARCHAR(12) NOT NULL CHECK (type IN ('thumbnail', 'file')),
    size          BIGINT NOT NULL,
    last_accessed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id, path, type)
    );

-- File operations (move / copy / rename)
CREATE TABLE IF NOT EXISTS operations
(
    id               SERIAL PRIMARY KEY,
    operation        VARCHAR(12) NOT NULL CHECK (operation IN ('move', 'copy', 'rename')),
    target           VARCHAR(12) NOT NULL CHECK (target IN ('file', 'directory')),
    fs_entry_id      INTEGER REFERENCES fs_entry (id) ON DELETE CASCADE,
    source_path      TEXT NOT NULL CHECK (source_path LIKE '/%'),
    destination_path TEXT NOT NULL CHECK (destination_path LIKE '/%'),
    executed_by      INTEGER REFERENCES users (id),
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    status           VARCHAR(20) NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending', 'in_progress', 'success', 'error', 'cancelled')),
    completed_at     TIMESTAMP DEFAULT NULL,
    error            TEXT
    );

-- Extended attributes
CREATE TABLE IF NOT EXISTS file_xattrs
(
    id         SERIAL PRIMARY KEY,
    file_id    INTEGER NOT NULL REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    namespace  VARCHAR(64) NOT NULL DEFAULT 'user',
    key        VARCHAR(255) NOT NULL,
    value      BYTEA NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (file_id, namespace, key)
    );

-- File versioning
CREATE TABLE IF NOT EXISTS file_versions
(
    id             SERIAL PRIMARY KEY,
    file_id        INTEGER REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    version_number INTEGER NOT NULL,
    content_hash   VARCHAR(128) NOT NULL,
    size_bytes     BIGINT NOT NULL,
    mime_type      VARCHAR(255),
    path           TEXT NOT NULL,
    created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    created_by     INTEGER REFERENCES users (id),
    UNIQUE (file_id, version_number)
    );

-- ##################################
-- File Metadata (Typed)
-- ##################################

DO $$ BEGIN
CREATE TYPE file_metadata_type AS ENUM ('string', 'integer', 'boolean', 'timestamp', 'float');
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

CREATE TABLE IF NOT EXISTS file_metadata
(
    id         SERIAL PRIMARY KEY,
    file_id    INTEGER REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    key        VARCHAR(255) NOT NULL,
    type       file_metadata_type NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (file_id, key)
    );

CREATE TABLE IF NOT EXISTS file_metadata_string
(
    file_metadata_id INTEGER PRIMARY KEY REFERENCES file_metadata (id) ON DELETE CASCADE,
    value            TEXT NOT NULL
    );

CREATE TABLE IF NOT EXISTS file_metadata_integer
(
    file_metadata_id INTEGER PRIMARY KEY REFERENCES file_metadata (id) ON DELETE CASCADE,
    value            BIGINT NOT NULL
    );

CREATE TABLE IF NOT EXISTS file_metadata_boolean
(
    file_metadata_id INTEGER PRIMARY KEY REFERENCES file_metadata (id) ON DELETE CASCADE,
    value            BOOLEAN NOT NULL
    );

CREATE TABLE IF NOT EXISTS file_metadata_timestamp
(
    file_metadata_id INTEGER PRIMARY KEY REFERENCES file_metadata (id) ON DELETE CASCADE,
    value            TIMESTAMP NOT NULL
    );

CREATE TABLE IF NOT EXISTS file_metadata_float
(
    file_metadata_id INTEGER PRIMARY KEY REFERENCES file_metadata (id) ON DELETE CASCADE,
    value            DOUBLE PRECISION NOT NULL
    );

-- Locks, thumbnails, audit
CREATE TABLE IF NOT EXISTS file_locks
(
    file_id    INTEGER PRIMARY KEY REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    locked_by  INTEGER REFERENCES users (id),
    locked_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP
    );

CREATE TABLE IF NOT EXISTS thumbnails
(
    vault_id     INTEGER REFERENCES vault (id),
    path         TEXT NOT NULL,
    updated_at   TIMESTAMP NOT NULL,
    content_hash TEXT NOT NULL,
    width        INTEGER,
    height       INTEGER,
    cached_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (vault_id, path)
    );

CREATE TABLE IF NOT EXISTS audit_log
(
    id             SERIAL PRIMARY KEY,
    user_id        INTEGER REFERENCES users (id),
    action         VARCHAR(100) NOT NULL,
    target_file_id INTEGER REFERENCES files (fs_entry_id),
    timestamp      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    ip_address     VARCHAR(50),
    user_agent     TEXT
    );

CREATE TABLE IF NOT EXISTS file_activity
(
    id        SERIAL PRIMARY KEY,
    file_id   INTEGER REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    user_id   INTEGER REFERENCES users (id),
    action    VARCHAR(100) NOT NULL,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );

CREATE TABLE IF NOT EXISTS file_tags
(
    id         SERIAL PRIMARY KEY,
    name       VARCHAR(100) UNIQUE NOT NULL,
    color      VARCHAR(20) DEFAULT '#FFFFFF',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );

CREATE TABLE IF NOT EXISTS file_tag_assignments
(
    file_id     INTEGER REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    tag_id      INTEGER REFERENCES file_tags (id) ON DELETE CASCADE,
    assigned_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (file_id, tag_id)
    );

CREATE TABLE IF NOT EXISTS file_search_index
(
    file_id       INTEGER PRIMARY KEY REFERENCES files (fs_entry_id) ON DELETE CASCADE,
    search_vector tsvector
    );

-- ##################################
-- Indexes
-- ##################################

CREATE INDEX IF NOT EXISTS idx_fs_entry_vault_path
    ON fs_entry (vault_id, path text_pattern_ops);

CREATE INDEX IF NOT EXISTS idx_fs_entry_parent
    ON fs_entry (parent_id);

CREATE INDEX IF NOT EXISTS idx_files_trashed_backing_path_not_deleted
    ON files_trashed (backing_path) WHERE deleted_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_files_trashed_entry_not_deleted
    ON files_trashed (id) WHERE deleted_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_files_trashed_deleted_at
    ON files_trashed (deleted_at);

CREATE INDEX IF NOT EXISTS idx_files_trashed_vault_trashed_at
    ON files_trashed (vault_id, trashed_at DESC) WHERE deleted_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_cache_index_size
    ON cache_index (size DESC);

CREATE INDEX IF NOT EXISTS idx_cache_index_vault_type_size
    ON cache_index (vault_id, type, size DESC);

CREATE INDEX IF NOT EXISTS file_search_vector_idx
    ON file_search_index USING GIN (search_vector);

CREATE INDEX IF NOT EXISTS idx_file_xattrs_file_id
    ON file_xattrs (file_id);

CREATE INDEX IF NOT EXISTS idx_file_xattrs_key
    ON file_xattrs (key);

CREATE INDEX IF NOT EXISTS idx_file_metadata_key
    ON file_metadata (key);


DO $$ BEGIN
CREATE TRIGGER set_fs_entry_updated_at
    BEFORE UPDATE ON fs_entry
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
