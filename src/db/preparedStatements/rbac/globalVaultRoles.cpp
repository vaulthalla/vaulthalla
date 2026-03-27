#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedGlobalVaultRoles() const {
    conn_->prepare(
        "user_global_vault_policy_upsert",
        R"SQL(
            INSERT INTO user_global_vault_policy (
                user_id,
                template_role_id,
                enforce_template,
                scope,
                files_permissions,
                directories_permissions,
                sync_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3,
                $4::global_name,
                $5::bit(64),
                $6::bit(64),
                $7::bit(32),
                $8::bit(16)
            )
            ON CONFLICT (user_id, scope) DO UPDATE SET
                template_role_id        = EXCLUDED.template_role_id,
                enforce_template        = EXCLUDED.enforce_template,
                files_permissions       = EXCLUDED.files_permissions,
                directories_permissions = EXCLUDED.directories_permissions,
                sync_permissions        = EXCLUDED.sync_permissions,
                roles_permissions       = EXCLUDED.roles_permissions
            RETURNING
                user_id,
                template_role_id,
                enforce_template,
                scope::text                    AS scope,
                created_at,
                updated_at,
                files_permissions::text        AS files_permissions,
                directories_permissions::text  AS directories_permissions,
                sync_permissions::text         AS sync_permissions,
                roles_permissions::text        AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_insert",
        R"SQL(
            INSERT INTO user_global_vault_policy (
                user_id,
                template_role_id,
                enforce_template,
                scope,
                files_permissions,
                directories_permissions,
                sync_permissions,
                roles_permissions
            )
            VALUES (
                $1,
                $2,
                $3,
                $4::global_name,
                $5::bit(64),
                $6::bit(64),
                $7::bit(32),
                $8::bit(16)
            )
            RETURNING
                user_id,
                template_role_id,
                enforce_template,
                scope::text                    AS scope,
                created_at,
                updated_at,
                files_permissions::text        AS files_permissions,
                directories_permissions::text  AS directories_permissions,
                sync_permissions::text         AS sync_permissions,
                roles_permissions::text        AS roles_permissions
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_get",
        R"SQL(
            SELECT
                user_id,
                template_role_id,
                enforce_template,
                scope::text                    AS scope,
                created_at,
                updated_at,
                files_permissions::text        AS files_permissions,
                directories_permissions::text  AS directories_permissions,
                sync_permissions::text         AS sync_permissions,
                roles_permissions::text        AS roles_permissions
            FROM user_global_vault_policy
            WHERE user_id = $1
              AND scope = $2::global_name
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_update",
        R"SQL(
            UPDATE user_global_vault_policy
            SET
                template_role_id        = $3,
                enforce_template        = $4,
                files_permissions       = $5::bit(64),
                directories_permissions = $6::bit(64),
                sync_permissions        = $7::bit(32),
                roles_permissions       = $8::bit(16)
            WHERE user_id = $1
              AND scope = $2::global_name
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_delete",
        R"SQL(
            DELETE FROM user_global_vault_policy
            WHERE user_id = $1
              AND scope = $2::global_name
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_delete_all_for_user",
        R"SQL(
            DELETE FROM user_global_vault_policy
            WHERE user_id = $1
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_exists",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM user_global_vault_policy
                WHERE user_id = $1
                  AND scope = $2::global_name
            )
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_list_by_user",
        R"SQL(
            SELECT
                user_id,
                template_role_id,
                enforce_template,
                scope::text                    AS scope,
                created_at,
                updated_at,
                files_permissions::text        AS files_permissions,
                directories_permissions::text  AS directories_permissions,
                sync_permissions::text         AS sync_permissions,
                roles_permissions::text        AS roles_permissions
            FROM user_global_vault_policy
            WHERE user_id = $1
            ORDER BY scope
        )SQL"
    );

    conn_->prepare(
        "user_global_vault_policy_list_all",
        R"SQL(
            SELECT
                user_id,
                template_role_id,
                enforce_template,
                scope::text                    AS scope,
                created_at,
                updated_at,
                files_permissions::text        AS files_permissions,
                directories_permissions::text  AS directories_permissions,
                sync_permissions::text         AS sync_permissions,
                roles_permissions::text        AS roles_permissions
            FROM user_global_vault_policy
            ORDER BY user_id, scope
        )SQL"
    );
}
