-- ##################################
-- Vaults & Providers Schema
-- ##################################

CREATE TABLE IF NOT EXISTS api_keys
(
    id                          SERIAL PRIMARY KEY,
    user_id                     INTEGER REFERENCES users (id) ON DELETE CASCADE,
    name                        VARCHAR(100) NOT NULL,
    provider                    VARCHAR(50) NOT NULL,
    access_key                  TEXT NOT NULL,
    encrypted_secret_access_key BYTEA NOT NULL,
    iv                          BYTEA NOT NULL,
    region                      VARCHAR(20) NOT NULL,
    endpoint                    TEXT DEFAULT NULL,
    created_at                  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, name, access_key)
    );

CREATE TABLE IF NOT EXISTS vault
(
    id             SERIAL PRIMARY KEY,
    type           VARCHAR(12) NOT NULL CHECK (type IN ('local', 's3')),
    name           VARCHAR(100) UNIQUE NOT NULL,
    description    TEXT DEFAULT NULL,
    quota          BIGINT NOT NULL DEFAULT (0),
    owner_id       INTEGER REFERENCES users (id) ON DELETE SET NULL,
    mount_point    CHAR(33) NOT NULL,
    allow_fs_write BOOLEAN DEFAULT FALSE,
    is_active      BOOLEAN DEFAULT TRUE,
    created_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at     TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (name, owner_id)
    );

CREATE TABLE IF NOT EXISTS vault_keys
(
    vault_id      SERIAL PRIMARY KEY REFERENCES vault (id) ON DELETE CASCADE,
    encrypted_key BYTEA NOT NULL,
    iv            BYTEA NOT NULL,
    created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    version       INTEGER NOT NULL DEFAULT 1,
    UNIQUE (vault_id)
    );

CREATE TABLE IF NOT EXISTS vault_keys_trashed
(
    id                    SERIAL PRIMARY KEY,
    vault_id              INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    version               INTEGER NOT NULL DEFAULT 1,
    encrypted_key         BYTEA NOT NULL,
    iv                    BYTEA NOT NULL,
    created_at            TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    trashed_at            TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    rotation_completed_at TIMESTAMP DEFAULT NULL,
    UNIQUE (vault_id, version)
    );

CREATE TABLE IF NOT EXISTS s3
(
    vault_id         INTEGER PRIMARY KEY REFERENCES vault (id) ON DELETE CASCADE,
    api_key_id       INTEGER REFERENCES api_keys (id) ON DELETE CASCADE,
    bucket           TEXT NOT NULL,
    encrypt_upstream BOOLEAN DEFAULT TRUE,
    UNIQUE (api_key_id, bucket)
    );

CREATE TABLE IF NOT EXISTS vault_usage
(
    vault_id     INTEGER PRIMARY KEY REFERENCES vault (id) ON DELETE CASCADE,
    total_bytes  BIGINT DEFAULT 0 NOT NULL,
    quota_bytes  BIGINT DEFAULT NULL,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );

CREATE TABLE IF NOT EXISTS usage_log
(
    id          SERIAL PRIMARY KEY,
    vault_id    INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    measured_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    total_bytes BIGINT NOT NULL
    );

-- Triggers

DO $$ BEGIN
CREATE TRIGGER set_vault_updated_at
    BEFORE UPDATE ON vault
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

DO $$ BEGIN
CREATE TRIGGER set_vault_keys_updated_at
    BEFORE UPDATE ON vault_keys
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

DO $$ BEGIN
CREATE TRIGGER set_vault_usage_updated_at
    BEFORE UPDATE ON vault_usage
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;
