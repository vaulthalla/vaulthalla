#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedVaultRoles() const {
    conn_->prepare(
        "vault_role_upsert",
        R"SQL(
            INSERT INTO vault_role (
                id,
                name,
                description,
                files_permissions,
                directories_permissions,
                sync_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3,
                $4::bit(32),
                $5::bit(32),
                $6::bit(32),
                $7::bit(16)
            )
            ON CONFLICT (id) DO UPDATE SET
                name                    = EXCLUDED.name,
                description             = EXCLUDED.description,
                files_permissions       = EXCLUDED.files_permissions,
                directories_permissions = EXCLUDED.directories_permissions,
                sync_permissions        = EXCLUDED.sync_permissions,
                roles_permissions       = EXCLUDED.roles_permissions
        )SQL"
    );

    // DELETE THIS IF NOT USED
    conn_->prepare(
        "vault_role_upsert_by_name",
        R"SQL(
            INSERT INTO vault_role (
                name,
                description,
                files_permissions,
                directories_permissions,
                sync_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3::bit(32),
                $4::bit(32),
                $5::bit(32),
                $6::bit(16)
            )
            ON CONFLICT (name) DO UPDATE SET
                description             = EXCLUDED.description,
                files_permissions       = EXCLUDED.files_permissions,
                directories_permissions = EXCLUDED.directories_permissions,
                sync_permissions        = EXCLUDED.sync_permissions,
                roles_permissions       = EXCLUDED.roles_permissions
            RETURNING
                id,
                name,
                description,
                created_at,
                updated_at,
                files_permissions::bigint       AS files_permissions,
                directories_permissions::bigint AS directories_permissions,
                sync_permissions::bigint        AS sync_permissions,
                roles_permissions::bigint       AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "vault_role_insert",
        R"SQL(
            INSERT INTO vault_role (
                name,
                description,
                files_permissions,
                directories_permissions,
                sync_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3::bit(32),
                $4::bit(32),
                $5::bit(32),
                $6::bit(16)
            )
            RETURNING
                id,
                name,
                description,
                created_at,
                updated_at,
                files_permissions::bigint       AS files_permissions,
                directories_permissions::bigint AS directories_permissions,
                sync_permissions::bigint        AS sync_permissions,
                roles_permissions::bigint       AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "vault_role_get_by_id",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                files_permissions::bigint       AS files_permissions,
                directories_permissions::bigint AS directories_permissions,
                sync_permissions::bigint        AS sync_permissions,
                roles_permissions::bigint       AS roles_permissions
            FROM vault_role
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_get_by_name",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                files_permissions::bigint       AS files_permissions,
                directories_permissions::bigint AS directories_permissions,
                sync_permissions::bigint        AS sync_permissions,
                roles_permissions::bigint       AS roles_permissions
            FROM vault_role
            WHERE name = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_update",
        R"SQL(
            UPDATE vault_role
            SET
                name                    = $2,
                description             = $3,
                files_permissions       = $4::bit(32),
                directories_permissions = $5::bit(32),
                sync_permissions        = $6::bit(32),
                roles_permissions       = $7::bit(16)
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_delete",
        R"SQL(
            DELETE FROM vault_role
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_exists_by_id",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM vault_role
                WHERE id = $1
            )
        )SQL"
    );

    conn_->prepare(
        "vault_role_exists_by_name",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM vault_role
                WHERE name = $1
            )
        )SQL"
    );

    conn_->prepare(
        "vault_role_list_all",
        R"SQL(
            SELECT
                id,
                name,
                description,
                created_at,
                updated_at,
                files_permissions::bigint       AS files_permissions,
                directories_permissions::bigint AS directories_permissions,
                sync_permissions::bigint        AS sync_permissions,
                roles_permissions::bigint       AS roles_permissions
            FROM vault_role
            ORDER BY name
        )SQL"
    );
}
