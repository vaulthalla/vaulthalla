#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedVaultRecovery() const {
    conn_->prepare("vault_recovery.policy",
        R"SQL(
            SELECT
                enabled,
                status,
                backup_interval,
                last_backup_at,
                last_success_at,
                EXTRACT(EPOCH FROM retention) AS retention_seconds,
                retry_count,
                last_error
            FROM backup_policy
            WHERE vault_id = $1
            ORDER BY id DESC
            LIMIT 1;
        )SQL"
    );
}
