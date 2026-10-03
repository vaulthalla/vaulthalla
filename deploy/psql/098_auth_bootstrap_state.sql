-- Instance-level bootstrap state of the built-in web super-admin ('admin') credential. One row (id = 1).
--
-- Fresh installs give 'admin' a random per-install password and write a plaintext copy to
-- /var/lib/vaulthalla/super_admin_initial_password for the operator. This row records whether the current password
-- is still that generated one; whether the copy still exists is read from the filesystem, never from here.
-- Installs created before this table have no generated credential: the row starts FALSE, and the daemon replaces a
-- password still equal to the retired universal default at startup (see auth::bootstrap).

CREATE TABLE IF NOT EXISTS auth_bootstrap_state
(
    id                             SMALLINT PRIMARY KEY DEFAULT 1 CHECK (id = 1),
    super_admin_password_generated BOOLEAN     NOT NULL DEFAULT FALSE,
    generated_at                   TIMESTAMPTZ,
    rotated_at                     TIMESTAMPTZ
);

INSERT INTO auth_bootstrap_state (id) VALUES (1) ON CONFLICT (id) DO NOTHING;
