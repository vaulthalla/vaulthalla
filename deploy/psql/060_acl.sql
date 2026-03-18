-- ##################################
-- Access Control List (ACL) Schema
-- ##################################

-- ACL assignments (depends on: users, vault, role, permission)
CREATE TABLE IF NOT EXISTS permission
(
    id           SERIAL PRIMARY KEY,
    name         VARCHAR(50) NOT NULL,
    description  TEXT,
    category     permission_categories NOT NULL,
    bit_position INTEGER NOT NULL CHECK (bit_position >= 0 AND bit_position < 64),
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (name, bit_position, category)
    );

-- Permissions assigned to roles
CREATE TABLE IF NOT EXISTS vault_role
(
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(50) UNIQUE NOT NULL,
    description TEXT,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    files_permissions       BIT(32) NOT NULL,
    directories_permissions BIT(32) NOT NULL,
    sync_permissions        BIT(32) NOT NULL,
    roles_permissions       BIT(16) NOT NULL
);

CREATE TABLE IF NOT EXISTS admin_role
(
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(50) UNIQUE NOT NULL,
    description TEXT,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    identity_permissions BIT(32) NOT NULL,
    audit_permissions    BIT(8) NOT NULL,
    settings_permissions BIT(16) NOT NULL,
    roles_permissions    BIT(16) NOT NULL,
    vaults_permissions   BIT(32) NOT NULL,
    keys_permissions     BIT(32) NOT NULL
);

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_type
        WHERE typname = 'global_name'
    ) THEN
CREATE TYPE global_name AS ENUM ('self', 'user', 'admin');
END IF;
END
$$;

CREATE TABLE IF NOT EXISTS user_global_vault_policy
(
    user_id     INTEGER NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    template_role_id  INTEGER DEFAULT NULL REFERENCES vault_role (id) ON DELETE SET NULL,
    enforce_template  BOOLEAN NOT NULL DEFAULT FALSE
    CHECK (NOT enforce_template OR template_role_id IS NOT NULL),

    scope       global_name NOT NULL,

    files_permissions       BIT(64) NOT NULL,
    directories_permissions BIT(64) NOT NULL,
    sync_permissions        BIT(32) NOT NULL,
    roles_permissions       BIT(16) NOT NULL,

    PRIMARY KEY (user_id, scope)
    );

-- Assignments: user-level
CREATE TABLE IF NOT EXISTS admin_role_assignments
(
    id          SERIAL PRIMARY KEY,
    user_id     INTEGER UNIQUE NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    role_id     INTEGER NOT NULL REFERENCES admin_role (id) ON DELETE CASCADE,
    assigned_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    );

-- Assignments: vault-level (user / group)
CREATE TABLE IF NOT EXISTS vault_role_assignments
(
    id           SERIAL PRIMARY KEY,
    vault_id     INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    subject_type VARCHAR(10) NOT NULL CHECK (subject_type IN ('user', 'group')),
    subject_id   INTEGER NOT NULL,
    role_id      INTEGER NOT NULL REFERENCES vault_role (id) ON DELETE CASCADE,
    assigned_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id, subject_type, subject_id)
    );

-- Permission overrides (fine-grained ACL rules)
CREATE TABLE IF NOT EXISTS vault_permission_overrides
(
    id            SERIAL PRIMARY KEY,
    assignment_id INTEGER NOT NULL REFERENCES vault_role_assignments (id) ON DELETE CASCADE,
    permission_id INTEGER NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    glob_path     TEXT        NOT NULL,
    enabled       BOOLEAN     DEFAULT TRUE,
    effect        VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at    TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (assignment_id, permission_id, glob_path)
);

-- ##################################
-- Indexes
-- ##################################

CREATE UNIQUE INDEX IF NOT EXISTS idx_permission_name_category
    ON permission (LOWER(name), category);

CREATE INDEX IF NOT EXISTS idx_vault_role_assignments_vault_subject
    ON vault_role_assignments (vault_id, subject_type, subject_id);

CREATE INDEX IF NOT EXISTS idx_vault_perm_override_assignment
    ON vault_permission_overrides (assignment_id);

CREATE INDEX IF NOT EXISTS idx_vault_permission_overrides_perm
    ON vault_permission_overrides (permission_id);

CREATE INDEX IF NOT EXISTS idx_vault_permission_overrides_assignment_perm
    ON vault_permission_overrides (assignment_id, permission_id);


-- ##################################
-- Trigger Functions
-- ##################################

CREATE OR REPLACE FUNCTION sync_enforce_template_with_template_null()
RETURNS TRIGGER AS $$
BEGIN
    IF NEW.template_role_id IS NULL THEN
        NEW.enforce_template = FALSE;
END IF;

RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- ##################################
-- Triggers
-- ##################################

DROP TRIGGER IF EXISTS trg_sync_enforce_template_with_template_null
    ON user_global_vault_policy;

CREATE TRIGGER trg_sync_enforce_template_with_template_null
    BEFORE INSERT OR UPDATE ON user_global_vault_policy
                         FOR EACH ROW
                         EXECUTE FUNCTION sync_enforce_template_with_template_null();


DROP TRIGGER IF EXISTS trg_set_updated_at_vault_role ON vault_role;
CREATE TRIGGER trg_set_updated_at_vault_role
    BEFORE UPDATE ON vault_role
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_admin_role ON admin_role;
CREATE TRIGGER trg_set_updated_at_admin_role
    BEFORE UPDATE ON admin_role
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_user_global_vault_policy ON user_global_vault_policy;
CREATE TRIGGER trg_set_updated_at_user_global_vault_policy
    BEFORE UPDATE ON user_global_vault_policy
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_set_updated_at_vault_permission_overrides ON vault_permission_overrides;
CREATE TRIGGER trg_set_updated_at_vault_permission_overrides
    BEFORE UPDATE ON vault_permission_overrides
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER set_updated_at
    BEFORE UPDATE ON permission
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
