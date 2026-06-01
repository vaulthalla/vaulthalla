CREATE TABLE IF NOT EXISTS s3_gateway_bucket
(
    vault_id       INTEGER PRIMARY KEY REFERENCES vault(id) ON DELETE CASCADE,
    bucket_name    TEXT UNIQUE NOT NULL,
    api_exclusive  BOOLEAN NOT NULL DEFAULT FALSE,
    mode           VARCHAR(16) NOT NULL DEFAULT 'local'
        CHECK (mode IN ('local', 'remote_cache', 'remote_proxy')),
    created_by     INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_bucket_name
    ON s3_gateway_bucket(bucket_name);

CREATE TABLE IF NOT EXISTS s3_gateway_credentials
(
    id                          SERIAL PRIMARY KEY,
    user_id                     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name                        VARCHAR(100) NOT NULL,
    access_key                  TEXT UNIQUE NOT NULL,
    encrypted_secret_access_key BYTEA NOT NULL,
    iv                          BYTEA NOT NULL,
    enabled                     BOOLEAN NOT NULL DEFAULT TRUE,
    enforce_budget_for_local_requests BOOLEAN NOT NULL DEFAULT FALSE,
    created_at                  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_used_at                TIMESTAMP DEFAULT NULL,
    UNIQUE(user_id, name)
);

CREATE TABLE IF NOT EXISTS s3_gateway_object
(
    vault_id       INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key     TEXT NOT NULL,
    etag           TEXT NOT NULL,
    size_bytes     BIGINT NOT NULL DEFAULT 0,
    content_type   TEXT DEFAULT NULL,
    storage_class  TEXT DEFAULT NULL,
    last_modified  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    multipart      BOOLEAN NOT NULL DEFAULT FALSE,
    part_count     INTEGER DEFAULT NULL,
    PRIMARY KEY (vault_id, object_key)
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_object_vault_key
    ON s3_gateway_object(vault_id, object_key text_pattern_ops);

CREATE TABLE IF NOT EXISTS s3_gateway_object_metadata
(
    vault_id    INTEGER NOT NULL,
    object_key  TEXT NOT NULL,
    name        TEXT NOT NULL,
    value       TEXT NOT NULL,
    PRIMARY KEY (vault_id, object_key, name),
    FOREIGN KEY (vault_id, object_key)
        REFERENCES s3_gateway_object(vault_id, object_key)
        ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS s3_gateway_multipart_upload
(
    upload_id     TEXT PRIMARY KEY,
    vault_id      INTEGER NOT NULL REFERENCES vault(id) ON DELETE CASCADE,
    object_key    TEXT NOT NULL,
    initiated_by  INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    initiated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    content_type  TEXT DEFAULT NULL,
    metadata      JSONB NOT NULL DEFAULT '{}'::jsonb,
    storage_class TEXT DEFAULT NULL,
    aborted       BOOLEAN NOT NULL DEFAULT FALSE,
    completed     BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_s3_gateway_multipart_upload_vault_key
    ON s3_gateway_multipart_upload(vault_id, object_key);

CREATE TABLE IF NOT EXISTS s3_gateway_multipart_part
(
    upload_id   TEXT NOT NULL REFERENCES s3_gateway_multipart_upload(upload_id) ON DELETE CASCADE,
    part_number INTEGER NOT NULL CHECK (part_number BETWEEN 1 AND 10000),
    etag        TEXT NOT NULL,
    size_bytes  BIGINT NOT NULL,
    md5         BYTEA NOT NULL,
    path        TEXT NOT NULL,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(upload_id, part_number)
);
