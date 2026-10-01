-- v1.6.0 edited 060_acl.sql in place to add admin_role.s3_gateway_permissions.
--
-- Databases initialized before v1.6.0 recorded the old 060_acl.sql checksum and never received the column.
-- SqlDeployer accepts that old checksum (kHistoricalMigrationChecksums) without re-running 060, and this
-- migration brings those databases to the v1.6.0 shape.
--
-- Databases that ran the new 060_acl.sql already have the column: the whole block is skipped, so this is a
-- no-op there (including for any admin_role permissions an operator changed since).
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'admin_role'
          AND column_name = 's3_gateway_permissions'
    ) THEN
        -- Same definition as 060_acl.sql (v1.6.0+).
        ALTER TABLE admin_role
            ADD COLUMN s3_gateway_permissions BIT(8) NOT NULL DEFAULT B'00000000';

        -- Built-in admin roles on a fresh v1.6+ install are seeded with S3 gateway permissions
        -- (core/include/rbac/role/Admin.hpp). Seeding does not re-run on upgraded installs, so give the
        -- built-in templates the same bits a fresh install gets. Custom roles keep the least-privilege default.
        -- Bit order (MSB..LSB): -, -, manage_budgets, manage_buckets, assign_principal, manage_credentials,
        -- manage_service, view.
        UPDATE admin_role
        SET s3_gateway_permissions = CASE name
            WHEN 'auditor'           THEN B'00000001' -- ViewOnly
            WHEN 'support'           THEN B'00000001' -- ViewOnly
            WHEN 'identity_admin'    THEN B'00000001' -- ViewOnly
            WHEN 'key_custodian'     THEN B'00000001' -- ViewOnly
            WHEN 'security_admin'    THEN B'00001101' -- PrincipalAssigner
            WHEN 'platform_operator' THEN B'00110011' -- Operator
            WHEN 'vault_admin'       THEN B'00110011' -- Operator
            WHEN 'admin'             THEN B'00111111' -- Full
            WHEN 'super_admin'       THEN B'00111111' -- Full
        END
        WHERE name IN (
            'auditor', 'support', 'identity_admin', 'key_custodian', 'security_admin',
            'platform_operator', 'vault_admin', 'admin', 'super_admin'
        );
    END IF;
END $$;
