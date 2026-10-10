-- #166: runtime stats get their own admin permission, admin.stats.view (admin_role.stats_permissions, bit 0 = view).
--
-- Server, daemon and system stats (the Health area, its severity dot, trends, caches, pricing totals) used to be
-- gated on User::isAdmin(), i.e. "can delete admins AND remove admin vaults". The built-in `admin` role has neither,
-- so it could not see Health. Vault-scoped stats are authorized per vault and do not use this bit.
--
-- The column is new, so the block runs once per database and is a no-op if the column already exists.
-- Built-in roles are seeded from core (core/src/rbac/role/Admin.cpp) only on a fresh database; on an upgraded one
-- the built-in roles that watch runtime health get the bit here (the same grants a fresh install seeds):
--   admin, auditor, platform_operator, super_admin.
-- Any other role that passed the old gate keeps stats access, so nobody loses Health on upgrade:
--   admin.identities.admins.delete = identity_permissions bit 11, admin.vaults.admin.remove = vaults_permissions
--   bit 12 (LSB = bit 0; get_bit counts from the leftmost bit, so BIT(32) bit k is get_bit(col, 31 - k)).
-- Every other custom role keeps the least-privilege default (no stats).
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'admin_role'
          AND column_name = 'stats_permissions'
    ) THEN
        ALTER TABLE admin_role
            ADD COLUMN stats_permissions BIT(8) NOT NULL DEFAULT B'00000000';

        UPDATE admin_role
        SET stats_permissions = B'00000001'
        WHERE name IN ('admin', 'auditor', 'platform_operator', 'super_admin')
           OR (get_bit(identity_permissions, 31 - 11) = 1 AND get_bit(vaults_permissions, 31 - 12) = 1);
    END IF;
END $$;

-- The permission catalog (permissions.list) is seeded from core only on a fresh database; list the new permission on
-- upgraded ones too. A fresh install's seed upserts the same row (bit 0 of the admin.stats module).
INSERT INTO permission (name, description, category, bit_position)
VALUES ('admin.stats.view', 'Allows viewing server, daemon and system stats and the health dashboard.', 'admin', 0)
ON CONFLICT DO NOTHING;
