#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedAdminRoles() const {
    conn_->prepare(
        "admin_role_upsert",
        R"SQL(
            INSERT INTO admin_role (
                id,
                name,
                description,
                identity_permissions,
                audit_permissions,
                settings_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3,
                $4::bit(32),
                $5::bit(8),
                $6::bit(16),
                $7::bit(16)
            )
            ON CONFLICT (id) DO UPDATE SET
                name                 = EXCLUDED.name,
                description          = EXCLUDED.description,
                identity_permissions = EXCLUDED.identity_permissions,
                audit_permissions    = EXCLUDED.audit_permissions,
                settings_permissions = EXCLUDED.settings_permissions,
                roles_permissions    = EXCLUDED.roles_permissions
        )SQL"
    );

    conn_->prepare(
        "admin_role_insert",
        R"SQL(
            INSERT INTO admin_role (
                name,
                description,
                identity_permissions,
                audit_permissions,
                settings_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3::bit(32),
                $4::bit(8),
                $5::bit(16),
                $6::bit(16)
            )
            RETURNING
                id,
                name,
                description,
                created_at,
                updated_at,
                identity_permissions::text AS identity_permissions,
                audit_permissions::text    AS audit_permissions,
                settings_permissions::text AS settings_permissions,
                roles_permissions::text    AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "admin_role_upsert_by_name",
        R"SQL(
            INSERT INTO admin_role (
                name,
                description,
                identity_permissions,
                audit_permissions,
                settings_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3::bit(32),
                $4::bit(8),
                $5::bit(16),
                $6::bit(16)
            )
            ON CONFLICT (name) DO UPDATE SET
                description          = EXCLUDED.description,
                identity_permissions = EXCLUDED.identity_permissions,
                audit_permissions    = EXCLUDED.audit_permissions,
                settings_permissions = EXCLUDED.settings_permissions,
                roles_permissions    = EXCLUDED.roles_permissions
            RETURNING
                id,
                name,
                description,
                created_at,
                updated_at,
                identity_permissions::text AS identity_permissions,
                audit_permissions::text    AS audit_permissions,
                settings_permissions::text AS settings_permissions,
                roles_permissions::text    AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "admin_role_get_by_id",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                identity_permissions::text AS identity_permissions,
                audit_permissions::text    AS audit_permissions,
                settings_permissions::text AS settings_permissions,
                roles_permissions::text    AS roles_permissions
            FROM admin_role
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "admin_role_get_by_name",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                identity_permissions::text AS identity_permissions,
                audit_permissions::text    AS audit_permissions,
                settings_permissions::text AS settings_permissions,
                roles_permissions::text    AS roles_permissions
            FROM admin_role
            WHERE name = $1
        )SQL"
    );

    conn_->prepare(
        "admin_role_update",
        R"SQL(
            UPDATE admin_role
            SET
                name                 = $2,
                description          = $3,
                identity_permissions = $4::bit(32),
                audit_permissions    = $5::bit(8),
                settings_permissions = $6::bit(16),
                roles_permissions    = $7::bit(16)
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "admin_role_delete",
        R"SQL(
            DELETE FROM admin_role
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "admin_role_exists_by_id",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM admin_role
                WHERE id = $1
            )
        )SQL"
    );

    conn_->prepare(
        "admin_role_exists_by_name",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM admin_role
                WHERE name = $1
            )
        )SQL"
    );

    conn_->prepare(
        "admin_role_list_all",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                identity_permissions::text AS identity_permissions,
                audit_permissions::text    AS audit_permissions,
                settings_permissions::text AS settings_permissions,
                roles_permissions::text    AS roles_permissions
            FROM admin_role
            ORDER BY name
        )SQL"
    );

    conn_->prepare(
        "admin_role_list_summaries",
        R"SQL(
            SELECT
                id,
                name,
                description
            FROM admin_role
            ORDER BY name
        )SQL"
    );
}
