#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedDashboardPreferences() const {
    conn_->prepare("dashboard_preferences.get_for_user",
        R"SQL(
            SELECT
                id,
                user_id,
                preference_key,
                layout_json::text AS layout_json,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                EXTRACT(EPOCH FROM updated_at)::bigint AS updated_at
            FROM dashboard_preferences
            WHERE user_id = $1
              AND preference_key = $2
            LIMIT 1;
        )SQL"
    );

    conn_->prepare("dashboard_preferences.upsert_for_user",
        R"SQL(
            INSERT INTO dashboard_preferences (user_id, preference_key, layout_json)
            VALUES ($1, $2, $3::jsonb)
            ON CONFLICT (user_id, preference_key)
            DO UPDATE SET layout_json = EXCLUDED.layout_json
            RETURNING
                id,
                user_id,
                preference_key,
                layout_json::text AS layout_json,
                EXTRACT(EPOCH FROM created_at)::bigint AS created_at,
                EXTRACT(EPOCH FROM updated_at)::bigint AS updated_at;
        )SQL"
    );

    conn_->prepare("dashboard_preferences.reset_for_user",
        R"SQL(
            WITH deleted AS (
                DELETE FROM dashboard_preferences
                WHERE user_id = $1
                  AND preference_key = $2
                RETURNING id
            )
            SELECT COUNT(*)::bigint AS deleted FROM deleted;
        )SQL"
    );
}
