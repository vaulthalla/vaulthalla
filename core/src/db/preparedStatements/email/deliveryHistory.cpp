#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedOperatorNotificationDelivery() const {
    conn_->prepare("operator_notification_delivery.insert", R"SQL(
        INSERT INTO operator_notification_delivery (
            event_key,
            event_type,
            severity,
            provider,
            subject,
            recipient_group,
            recipient_count,
            provider_message_id,
            status,
            error_summary,
            fingerprint,
            first_seen_at,
            last_seen_at,
            sent_at
        )
        VALUES (
            $1,
            $2,
            $3,
            $4,
            $5,
            $6,
            $7,
            $8,
            $9,
            $10,
            $11,
            CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP,
            CASE WHEN $9 = 'sent' THEN CURRENT_TIMESTAMP ELSE NULL END
        )
        RETURNING id
    )SQL");

    conn_->prepare("operator_notification_delivery.recent", R"SQL(
        SELECT *
        FROM operator_notification_delivery
        ORDER BY created_at DESC
        LIMIT $1
    )SQL");

    conn_->prepare("operator_notification_delivery.latest_for_event", R"SQL(
        SELECT *
        FROM operator_notification_delivery
        WHERE event_key = $1
          AND fingerprint = $2
        ORDER BY created_at DESC
        LIMIT 1
    )SQL");
}
