#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedShareVaultRoles() const {
    conn_->prepare(
        "share_vault_role_upsert_mapping",
        R"SQL(
            INSERT INTO link_share_vault_role (
                share_id,
                vault_id,
                vault_role_id
            )
            VALUES ($1, $2, $3)
            ON CONFLICT (share_id) DO UPDATE SET
                vault_id = EXCLUDED.vault_id,
                vault_role_id = EXCLUDED.vault_role_id,
                updated_at = CURRENT_TIMESTAMP
            RETURNING
                id,
                share_id,
                vault_id,
                vault_role_id,
                created_at,
                updated_at
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_get_by_share_id",
        R"SQL(
            SELECT
                lsvr.id                              AS share_vault_role_id,
                lsvr.share_id                        AS share_id,
                lsvr.vault_id                        AS vault_id,
                lsvr.vault_role_id                   AS vault_role_id,
                lsvr.created_at                      AS assigned_at,

                vr.name                              AS role_name,
                vr.description                       AS role_description,
                vr.created_at                        AS role_created_at,
                vr.updated_at                        AS role_updated_at,
                vr.files_permissions::bigint         AS files_permissions,
                vr.directories_permissions::bigint   AS directories_permissions,
                vr.sync_permissions::bigint          AS sync_permissions,
                vr.roles_permissions::bigint         AS roles_permissions
            FROM link_share_vault_role lsvr
            INNER JOIN vault_role vr
                ON vr.id = lsvr.vault_role_id
            WHERE lsvr.share_id = $1
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_delete_by_share_id",
        R"SQL(
            WITH mapped AS (
                DELETE FROM link_share_vault_role
                WHERE share_id = $1
                RETURNING vault_role_id
            )
            DELETE FROM vault_role
            WHERE id IN (SELECT vault_role_id FROM mapped)
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_delete_overrides",
        R"SQL(
            DELETE FROM link_share_vault_role_override
            WHERE link_share_vault_role_id = $1
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_override_insert",
        R"SQL(
            INSERT INTO link_share_vault_role_override (
                link_share_vault_role_id,
                permission_id,
                glob_path,
                enabled,
                effect
            )
            VALUES ($1, $2, $3, $4, $5)
            ON CONFLICT (link_share_vault_role_id, permission_id, glob_path)
            DO UPDATE SET
                enabled = EXCLUDED.enabled,
                effect = EXCLUDED.effect,
                updated_at = CURRENT_TIMESTAMP
            RETURNING id
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_permission_id_by_name",
        R"SQL(
            SELECT id
            FROM permission
            WHERE name = $1
            LIMIT 1
        )SQL"
    );

    conn_->prepare(
        "share_vault_role_override_list_by_mapping_id",
        R"SQL(
            SELECT
                lsvro.id                         AS override_id,
                lsvro.link_share_vault_role_id   AS assignment_id,
                lsvro.permission_id              AS permission_id,
                lsvro.glob_path                  AS glob_path,
                lsvro.enabled                    AS enabled,
                lsvro.effect                     AS effect,
                lsvro.created_at                 AS created_at,
                lsvro.updated_at                 AS updated_at,

                p.name                           AS name,
                p.description                    AS description,
                p.category                       AS category,
                p.bit_position                   AS bit_position
            FROM link_share_vault_role_override lsvro
            INNER JOIN permission p
                ON p.id = lsvro.permission_id
            WHERE lsvro.link_share_vault_role_id = $1
            ORDER BY p.bit_position, lsvro.glob_path
        )SQL"
    );
}
