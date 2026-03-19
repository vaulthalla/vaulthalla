#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedVaultRoleAssignments() const {
    conn_->prepare(
        "vault_role_assignment_upsert",
        R"SQL(
            INSERT INTO vault_role_assignments (
                vault_id,
                subject_type,
                subject_id,
                role_id
            )
            VALUES (
                $1,
                $2,
                $3,
                $4
            )
            ON CONFLICT (vault_id, subject_type, subject_id) DO UPDATE SET
                role_id = EXCLUDED.role_id,
                assigned_at = CURRENT_TIMESTAMP
            RETURNING
                id,
                vault_id,
                subject_type,
                subject_id,
                role_id,
                assigned_at
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_get",
        R"SQL(
            SELECT
                vra.id                           AS assignment_id,
                vra.vault_id                     AS vault_id,
                vra.subject_type                 AS subject_type,
                vra.subject_id                   AS subject_id,
                vra.role_id                      AS vault_role_id,
                vra.assigned_at                  AS assigned_at,

                vr.name                          AS role_name,
                vr.description                   AS role_description,
                vr.created_at                    AS role_created_at,
                vr.updated_at                    AS role_updated_at,
                vr.files_permissions::bigint       AS files_permissions,
                vr.directories_permissions::bigint AS directories_permissions,
                vr.sync_permissions::bigint        AS sync_permissions,
                vr.roles_permissions::bigint       AS roles_permissions
            FROM vault_role_assignments vra
            INNER JOIN vault_role vr
                ON vr.id = vra.role_id
            WHERE vra.vault_id = $1
              AND vra.subject_type = $2
              AND vra.subject_id = $3
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_get_by_id",
        R"SQL(
            SELECT
                vra.id                           AS assignment_id,
                vra.vault_id                     AS vault_id,
                vra.subject_type                 AS subject_type,
                vra.subject_id                   AS subject_id,
                vra.role_id                      AS vault_role_id,
                vra.assigned_at                  AS assigned_at,

                vr.name                          AS role_name,
                vr.description                   AS role_description,
                vr.created_at                    AS role_created_at,
                vr.updated_at                    AS role_updated_at,
                vr.files_permissions::bigint       AS files_permissions,
                vr.directories_permissions::bigint AS directories_permissions,
                vr.sync_permissions::bigint        AS sync_permissions,
                vr.roles_permissions::bigint       AS roles_permissions
            FROM vault_role_assignments vra
            INNER JOIN vault_role vr
                ON vr.id = vra.role_id
            WHERE vra.id = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_delete",
        R"SQL(
            DELETE FROM vault_role_assignments
            WHERE vault_id = $1
              AND subject_type = $2
              AND subject_id = $3
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_delete_by_id",
        R"SQL(
            DELETE FROM vault_role_assignments
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_exists",
        R"SQL(
            SELECT EXISTS(
                SELECT 1
                FROM vault_role_assignments
                WHERE vault_id = $1
                  AND subject_type = $2
                  AND subject_id = $3
            )
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_list_by_vault",
        R"SQL(
            SELECT
                vra.id                           AS assignment_id,
                vra.vault_id                     AS vault_id,
                vra.subject_type                 AS subject_type,
                vra.subject_id                   AS subject_id,
                vra.role_id                      AS vault_role_id,
                vra.assigned_at                  AS assigned_at,

                vr.name                          AS role_name,
                vr.description                   AS role_description,
                vr.created_at                    AS role_created_at,
                vr.updated_at                    AS role_updated_at,
                vr.files_permissions::bigint       AS files_permissions,
                vr.directories_permissions::bigint AS directories_permissions,
                vr.sync_permissions::bigint        AS sync_permissions,
                vr.roles_permissions::bigint       AS roles_permissions
            FROM vault_role_assignments vra
            INNER JOIN vault_role vr
                ON vr.id = vra.role_id
            WHERE vra.vault_id = $1
            ORDER BY vra.subject_type, vra.subject_id
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_list_by_subject",
        R"SQL(
            SELECT
                vra.id                           AS assignment_id,
                vra.vault_id                     AS vault_id,
                vra.subject_type                 AS subject_type,
                vra.subject_id                   AS subject_id,
                vra.role_id                      AS vault_role_id,
                vra.assigned_at                  AS assigned_at,

                vr.name                          AS role_name,
                vr.description                   AS role_description,
                vr.created_at                    AS role_created_at,
                vr.updated_at                    AS role_updated_at,
                vr.files_permissions::bigint       AS files_permissions,
                vr.directories_permissions::bigint AS directories_permissions,
                vr.sync_permissions::bigint        AS sync_permissions,
                vr.roles_permissions::bigint       AS roles_permissions
            FROM vault_role_assignments vra
            INNER JOIN vault_role vr
                ON vr.id = vra.role_id
            WHERE vra.subject_type = $1
              AND vra.subject_id = $2
            ORDER BY vra.vault_id
        )SQL"
    );

    conn_->prepare(
        "vault_role_assignment_list_all",
        R"SQL(
            SELECT
                vra.id                           AS assignment_id,
                vra.vault_id                     AS vault_id,
                vra.subject_type                 AS subject_type,
                vra.subject_id                   AS subject_id,
                vra.role_id                      AS vault_role_id,
                vra.assigned_at                  AS assigned_at,

                vr.name                          AS role_name,
                vr.description                   AS role_description,
                vr.created_at                    AS role_created_at,
                vr.updated_at                    AS role_updated_at,
                vr.files_permissions::bigint       AS files_permissions,
                vr.directories_permissions::bigint AS directories_permissions,
                vr.sync_permissions::bigint        AS sync_permissions,
                vr.roles_permissions::bigint       AS roles_permissions
            FROM vault_role_assignments vra
            INNER JOIN vault_role vr
                ON vr.id = vra.role_id
            ORDER BY vra.vault_id, vra.subject_type, vra.subject_id
        )SQL"
    );
}
