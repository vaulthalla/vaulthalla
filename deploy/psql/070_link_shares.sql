-- ##################################
-- Link Sharing Foundation Schema
-- ##################################

CREATE TABLE IF NOT EXISTS share_link
(
    id                      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    token_lookup_id         UUID UNIQUE NOT NULL,
    token_hash              BYTEA NOT NULL,
    created_by              INTEGER NOT NULL REFERENCES users (id),
    updated_by              INTEGER REFERENCES users (id),
    revoked_by              INTEGER REFERENCES users (id),
    vault_id                INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    root_entry_id           INTEGER NOT NULL REFERENCES fs_entry (id) ON DELETE CASCADE,
    root_path               TEXT NOT NULL CHECK (root_path LIKE '/%'),
    target_type             VARCHAR(16) NOT NULL CHECK (target_type IN ('file', 'directory')),
    link_type               VARCHAR(16) NOT NULL CHECK (link_type IN ('download', 'upload', 'access')),
    access_mode             VARCHAR(24) NOT NULL CHECK (access_mode IN ('public', 'email_validated')),
    allowed_ops             INTEGER NOT NULL DEFAULT 0,
    name                    VARCHAR(160),
    public_label            VARCHAR(160),
    description             TEXT,
    expires_at              TIMESTAMPTZ,
    revoked_at              TIMESTAMPTZ,
    disabled_at             TIMESTAMPTZ,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_accessed_at        TIMESTAMPTZ,
    access_count            BIGINT NOT NULL DEFAULT 0,
    download_count          BIGINT NOT NULL DEFAULT 0,
    upload_count            BIGINT NOT NULL DEFAULT 0,
    max_downloads           INTEGER,
    max_upload_files        INTEGER,
    max_upload_size_bytes   BIGINT,
    max_upload_total_bytes  BIGINT,
    duplicate_policy        VARCHAR(16) NOT NULL DEFAULT 'reject' CHECK (duplicate_policy IN ('reject', 'rename', 'overwrite')),
    allowed_mime_types      TEXT[],
    blocked_mime_types      TEXT[],
    allowed_extensions      TEXT[],
    blocked_extensions      TEXT[],
    metadata                JSONB NOT NULL DEFAULT '{}'::jsonb,

    CHECK (expires_at IS NULL OR expires_at > created_at),
    CHECK (max_downloads IS NULL OR max_downloads > 0),
    CHECK (max_upload_files IS NULL OR max_upload_files > 0),
    CHECK (max_upload_size_bytes IS NULL OR max_upload_size_bytes > 0),
    CHECK (max_upload_total_bytes IS NULL OR max_upload_total_bytes > 0),
    CHECK (link_type <> 'upload' OR target_type = 'directory'),
    CHECK (link_type <> 'download' OR (allowed_ops & 15) <> 0),
    CHECK (duplicate_policy <> 'overwrite' OR (allowed_ops & 64) <> 0),
    CHECK (target_type <> 'file' OR (allowed_ops & 32) = 0),
    CHECK (link_type <> 'upload' OR (allowed_ops & 8) = 0)
);

CREATE TABLE IF NOT EXISTS share_session
(
    id                        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    share_id                  UUID NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    session_token_lookup_id   UUID UNIQUE NOT NULL,
    session_token_hash        BYTEA NOT NULL,
    email_hash                BYTEA,
    verified_at               TIMESTAMPTZ,
    created_at                TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at              TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at                TIMESTAMPTZ NOT NULL,
    revoked_at                TIMESTAMPTZ,
    ip_address                VARCHAR(50),
    user_agent                TEXT
);

CREATE TABLE IF NOT EXISTS share_email_challenge
(
    id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    share_id           UUID NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    share_session_id   UUID REFERENCES share_session (id) ON DELETE CASCADE,
    email_hash         BYTEA NOT NULL,
    code_hash          BYTEA NOT NULL,
    attempts           INTEGER NOT NULL DEFAULT 0,
    max_attempts       INTEGER NOT NULL DEFAULT 6,
    expires_at         TIMESTAMPTZ NOT NULL,
    consumed_at        TIMESTAMPTZ,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ip_address         VARCHAR(50),
    user_agent         TEXT,

    CHECK (max_attempts > 0),
    CHECK (attempts >= 0),
    CHECK (expires_at > created_at)
);

CREATE TABLE IF NOT EXISTS share_upload
(
    id                    UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    share_id              UUID NOT NULL REFERENCES share_link (id) ON DELETE CASCADE,
    share_session_id      UUID NOT NULL REFERENCES share_session (id) ON DELETE CASCADE,
    target_parent_entry_id INTEGER NOT NULL REFERENCES fs_entry (id),
    target_path           TEXT NOT NULL CHECK (target_path LIKE '/%'),
    tmp_path              TEXT,
    original_filename     VARCHAR(500) NOT NULL,
    resolved_filename     VARCHAR(500) NOT NULL,
    expected_size_bytes   BIGINT NOT NULL,
    received_size_bytes   BIGINT NOT NULL DEFAULT 0,
    mime_type             VARCHAR(255),
    content_hash          VARCHAR(128),
    created_entry_id      INTEGER REFERENCES fs_entry (id) ON DELETE SET NULL,
    status                VARCHAR(16) NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'receiving', 'complete', 'failed', 'cancelled')),
    error                 TEXT,
    started_at            TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at          TIMESTAMPTZ,

    CHECK (expected_size_bytes >= 0),
    CHECK (received_size_bytes >= 0),
    CHECK (received_size_bytes <= expected_size_bytes)
);

CREATE TABLE IF NOT EXISTS share_access_event
(
    id                  BIGSERIAL PRIMARY KEY,
    share_id            UUID REFERENCES share_link (id) ON DELETE SET NULL,
    share_session_id    UUID REFERENCES share_session (id) ON DELETE SET NULL,
    actor_type          VARCHAR(24) NOT NULL CHECK (actor_type IN ('owner_user', 'admin_user', 'share_principal', 'system', 'unknown')),
    actor_user_id       INTEGER REFERENCES users (id),
    event_type          VARCHAR(48) NOT NULL,
    vault_id            INTEGER REFERENCES vault (id) ON DELETE SET NULL,
    target_entry_id     INTEGER REFERENCES fs_entry (id) ON DELETE SET NULL,
    target_path         TEXT,
    status              VARCHAR(16) NOT NULL CHECK (status IN ('success', 'denied', 'failed', 'rate_limited')),
    bytes_transferred   BIGINT,
    error_code          TEXT,
    error_message       TEXT,
    ip_address          VARCHAR(50),
    user_agent          TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CHECK (bytes_transferred IS NULL OR bytes_transferred >= 0)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_share_link_token_lookup
    ON share_link (token_lookup_id);

CREATE INDEX IF NOT EXISTS idx_share_link_vault_created
    ON share_link (vault_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_link_created_by
    ON share_link (created_by, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_link_root_entry
    ON share_link (root_entry_id);

CREATE INDEX IF NOT EXISTS idx_share_link_active_target
    ON share_link (vault_id, root_entry_id) WHERE revoked_at IS NULL;

CREATE UNIQUE INDEX IF NOT EXISTS idx_share_session_token_lookup
    ON share_session (session_token_lookup_id);

CREATE INDEX IF NOT EXISTS idx_share_session_share_expires
    ON share_session (share_id, expires_at);

CREATE INDEX IF NOT EXISTS idx_share_session_active
    ON share_session (share_id, expires_at) WHERE revoked_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_share_email_challenge_active
    ON share_email_challenge (share_id, email_hash, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_upload_share_status
    ON share_upload (share_id, status, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_access_event_share_created
    ON share_access_event (share_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_access_event_vault_created
    ON share_access_event (vault_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_share_access_event_type_created
    ON share_access_event (event_type, created_at DESC);

DO $$ BEGIN
CREATE TRIGGER set_share_link_updated_at
    BEFORE UPDATE ON share_link
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
