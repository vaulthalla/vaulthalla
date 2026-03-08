-- ##################################
-- Authentication & Identity Schema
-- ##################################

CREATE TABLE IF NOT EXISTS users
(
    id               SERIAL PRIMARY KEY,
    linux_uid        INTEGER UNIQUE,
    name             VARCHAR(80) UNIQUE NOT NULL,
    email            VARCHAR(255) UNIQUE DEFAULT NULL,
    password_hash    VARCHAR(255) NOT NULL,
    created_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login       TIMESTAMP,
    last_modified_by INTEGER REFERENCES users (id) ON DELETE SET NULL,
    updated_at       TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    is_active        BOOLEAN DEFAULT TRUE
);

CREATE TABLE IF NOT EXISTS refresh_tokens
(
    jti        UUID PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL,
    issued_at  TIMESTAMP DEFAULT NOW(),
    expires_at TIMESTAMP DEFAULT NOW() + INTERVAL '7 days',
    last_used  TIMESTAMP DEFAULT NOW(),
    ip_address TEXT,
    user_agent TEXT,
    revoked    BOOLEAN DEFAULT FALSE
);

CREATE TABLE IF NOT EXISTS groups
(
    id          SERIAL PRIMARY KEY,
    linux_gid   INTEGER UNIQUE,
    name        VARCHAR(50) UNIQUE NOT NULL,
    description TEXT,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS group_members
(
    group_id  INTEGER REFERENCES groups (id) ON DELETE CASCADE,
    user_id   INTEGER REFERENCES users (id) ON DELETE CASCADE,
    joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (group_id, user_id)
);

CREATE TABLE IF NOT EXISTS internal_secrets
(
    key        VARCHAR(50) PRIMARY KEY,
    value      BYTEA NOT NULL,
    iv         BYTEA NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
