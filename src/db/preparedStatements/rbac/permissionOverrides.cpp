#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedPermOverrides() const {
    conn_->prepare(
        "get_permission_override_by_vault_subject_and_bitpos",
        R"SQL(
            SELECT
                p.id AS permission_override_id,
                p.name,
                p.description,
                p.category,
                p.bit_position,
                p.created_at,
                p.updated_at,

                vpo.id AS override_id,
                vpo.enabled,
                vpo.glob_path,
                vpo.assignment_id,
                vpo.effect,
                vpo.created_at,
                vpo.updated_at,

                vra.role_id
            FROM permission p
            JOIN vault_permission_overrides vpo
                ON p.id = vpo.permission_id
            JOIN vault_role_assignments vra
                ON vpo.assignment_id = vra.id
            WHERE vra.vault_id = $1
              AND vra.subject_type = $2
              AND vra.subject_id = $3
              AND p.bit_position = $4
        )SQL"
    );

    conn_->prepare(
        "get_subject_assigned_vault_role_by_vault",
        R"SQL(
        SELECT
            vra.id AS assignment_id,
            vra.subject_type,
            vra.subject_id,
            vra.role_id,
            vra.assigned_at,

            vr.name,
            vr.description,
            vr.files_permissions,
            vr.directories_permissions,
            vr.sync_permissions,
            vr.roles_permissions,
            vr.created_at,
            vr.updated_at
        FROM vault_role vr
        JOIN vault_role_assignments vra
            ON vr.id = vra.role_id
        WHERE vra.vault_id = $1
          AND vra.subject_type = $2
          AND vra.subject_id = $3
    )SQL"
    );

    conn_->prepare(
        "list_vault_permission_overrides",
        R"SQL(
            SELECT
                p.id AS permission_override_id,
                p.name,
                p.description,
                p.category,
                p.bit_position,
                p.created_at,
                p.updated_at,

                vpo.id AS override_id,
                vpo.enabled,
                vpo.glob_path,
                vpo.assignment_id,
                vpo.effect,
                vpo.created_at,
                vpo.updated_at,

                vra.role_id
            FROM permission p
            JOIN vault_permission_overrides vpo
                ON p.id = vpo.permission_id
            JOIN vault_role_assignments vra
                ON vpo.assignment_id = vra.id
            WHERE vra.vault_id = $1
        )SQL"
    );

    conn_->prepare(
        "list_subject_permission_overrides",
        R"SQL(
            SELECT
                p.id AS permission_override_id,
                p.name,
                p.description,
                p.category,
                p.bit_position,
                p.created_at,
                p.updated_at,

                vpo.id AS override_id,
                vpo.enabled,
                vpo.glob_path,
                vpo.assignment_id,
                vpo.effect,
                vpo.created_at,
                vpo.updated_at,

                vra.role_id
            FROM permission p
            JOIN vault_permission_overrides vpo
                ON p.id = vpo.permission_id
            JOIN vault_role_assignments vra
                ON vpo.assignment_id = vra.id
            WHERE vra.subject_type = $1
              AND vra.subject_id = $2
        )SQL"
    );

    conn_->prepare(
        "list_user_and_group_permission_overrides",
        R"SQL(
            (
                SELECT
                    p.id AS permission_override_id,
                    p.name,
                    p.description,
                    p.category,
                    p.bit_position,
                    p.created_at,
                    p.updated_at,

                    vpo.id AS override_id,
                    vpo.enabled,
                    vpo.glob_path,
                    vpo.assignment_id,
                    vpo.effect,
                    vpo.created_at,
                    vpo.updated_at,

                    vra.role_id
                FROM permission p
                JOIN vault_permission_overrides vpo
                    ON p.id = vpo.permission_id
                JOIN vault_role_assignments vra
                    ON vpo.assignment_id = vra.id
                WHERE vra.subject_type = 'user'
                  AND vra.subject_id = $1
            )
            UNION ALL
            (
                SELECT
                    p.id AS permission_override_id,
                    p.name,
                    p.description,
                    p.category,
                    p.bit_position,
                    p.created_at,
                    p.updated_at,

                    vpo.id AS override_id,
                    vpo.enabled,
                    vpo.glob_path,
                    vpo.assignment_id,
                    vpo.effect,
                    vpo.created_at,
                    vpo.updated_at,

                    vra.role_id
                FROM permission p
                JOIN vault_permission_overrides vpo
                    ON p.id = vpo.permission_id
                JOIN vault_role_assignments vra
                    ON vpo.assignment_id = vra.id
                JOIN group_members gm
                    ON vra.subject_type = 'group'
                   AND vra.subject_id = gm.group_id
                WHERE gm.user_id = $1
            )
        )SQL"
    );

    conn_->prepare(
        "insert_vault_permission_override",
        R"SQL(
            INSERT INTO vault_permission_overrides (
                assignment_id,
                permission_id,
                glob_path,
                enabled,
                effect
            )
            VALUES ($1, $2, $3, $4, $5)
            RETURNING id
        )SQL"
    );

    conn_->prepare(
        "update_vault_permission_override",
        R"SQL(
            UPDATE vault_permission_overrides
            SET glob_path = $2,
                enabled = $3,
                effect = $4
            WHERE id = $1
        )SQL"
    );

    conn_->prepare(
        "delete_vault_permission_override",
        "DELETE FROM vault_permission_overrides WHERE id = $1"
    );

    conn_->prepare(
        "vault_permission_override_list_by_assignment_id",
        R"SQL(
            SELECT
                vpo.id AS override_id,
                vpo.assignment_id,
                vpo.permission_id,
                vpo.glob_path,
                vpo.enabled,
                vpo.effect,
                vpo.created_at,
                vpo.updated_at,

                vra.vault_id,
                vra.subject_type,
                vra.subject_id,
                vra.role_id,
                vra.assigned_at,

                p.id AS permission_override_id,
                p.name,
                p.description,
                p.category,
                p.bit_position
            FROM vault_permission_overrides vpo
            INNER JOIN permission p
                ON p.id = vpo.permission_id
            INNER JOIN vault_role_assignments vra
                ON vra.id = vpo.assignment_id
            WHERE vra.id = $1
            ORDER BY
                vra.vault_id,
                vra.subject_type,
                vra.subject_id,
                p.bit_position,
                vpo.glob_path
        )SQL"
    );
}
