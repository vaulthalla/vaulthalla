-- ##################################
-- Access Control List (ACL) Schema
-- ##################################

-- ACL assignments (depends on: users, vault, role, permission)

-- Permissions assigned to roles
CREATE TABLE IF NOT EXISTS vault_permissions
(
    role_id      INTEGER NOT NULL REFERENCES role(id) ON DELETE CASCADE,
    global_name  VARCHAR(8) NOT NULL CHECK (global_name IN ('role', 'self', 'user', 'admin')),

    files_permissions       BIT(64) NOT NULL,
    directories_permissions BIT(64) NOT NULL,
    sync_permissions        BIT(32) NOT NULL,
    roles_permissions       BIT(16) NOT NULL,
    keys_permissions        BIT(16) NOT NULL,

    PRIMARY KEY (role_id, global_name)
);

CREATE TABLE IF NOT EXISTS admin_permissions
(
    role_id INTEGER PRIMARY KEY REFERENCES role(id) ON DELETE CASCADE,

    identity_permissions BIT(32) NOT NULL,
    audit_permissions    BIT(8) NOT NULL,
    settings_permissions BIT(16) NOT NULL
);

-- Assignments: user-level
CREATE TABLE IF NOT EXISTS admin_role_assignments
(
    id          SERIAL PRIMARY KEY,
    user_id     INTEGER NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    role_id     INTEGER NOT NULL REFERENCES role (id) ON DELETE CASCADE,
    assigned_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, role_id)
    );

-- Assignments: vault-level (user / group)
CREATE TABLE IF NOT EXISTS vault_role_assignments
(
    id           SERIAL PRIMARY KEY,
    vault_id     INTEGER NOT NULL REFERENCES vault (id) ON DELETE CASCADE,
    subject_type VARCHAR(10) NOT NULL CHECK (subject_type IN ('user', 'group')),
    subject_id   INTEGER NOT NULL,
    role_id      INTEGER NOT NULL REFERENCES role (id) ON DELETE CASCADE,
    assigned_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (vault_id, subject_type, subject_id)
    );

-- Permission overrides (fine-grained ACL rules)
CREATE TABLE IF NOT EXISTS vault_permission_overrides
(
    id            SERIAL PRIMARY KEY,
    assignment_id INTEGER NOT NULL REFERENCES vault_role_assignments (id) ON DELETE CASCADE,
    permission_id INTEGER NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    permission_module  VARCHAR(12) NOT NULL CHECK (module IN ('filesystem', 'sync', 'roles', 'keys')),
    pattern       TEXT        NOT NULL,
    enabled       BOOLEAN     DEFAULT TRUE,
    effect        VARCHAR(10) NOT NULL CHECK (effect IN ('allow', 'deny')),
    created_at    TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (assignment_id, permission_id, pattern)
    );

-- ##################################
-- Indexes
-- ##################################

CREATE UNIQUE INDEX IF NOT EXISTS idx_permission_name_category
    ON permission (LOWER(name), category);

CREATE INDEX IF NOT EXISTS idx_vault_role_assignments_vault_subject
    ON vault_role_assignments (vault_id, subject_type, subject_id);

CREATE INDEX IF NOT EXISTS idx_vault_permission_overrides_perm
    ON vault_permission_overrides (permission_id);

CREATE INDEX IF NOT EXISTS idx_vault_permission_overrides_assignment_perm
    ON vault_permission_overrides (assignment_id, permission_id);
