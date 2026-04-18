#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedSync() const {
    conn_->prepare("insert_sync",
                   "INSERT INTO sync (vault_id, interval) "
                   "VALUES ($1, $2) RETURNING id");

    conn_->prepare("insert_sync_and_fsync",
                   "WITH ins AS ("
                   "  INSERT INTO sync (vault_id, interval) "
                   "  VALUES ($1, $2) RETURNING id"
                   ") "
                   "INSERT INTO fsync (sync_id, conflict_policy) "
                   "SELECT id, $3 FROM ins "
                   "RETURNING sync_id as id");

    conn_->prepare("insert_sync_and_rsync",
                   "WITH ins AS ("
                   "  INSERT INTO sync (vault_id, interval) "
                   "  VALUES ($1, $2) RETURNING id"
                   ") "
                   "INSERT INTO rsync (sync_id, conflict_policy, strategy) "
                   "SELECT id, $3, $4 FROM ins "
                   "RETURNING sync_id as id");

    conn_->prepare("update_sync_and_fsync",
                   "WITH updated_sync AS ("
                   "  UPDATE sync SET interval = $2, enabled = $3, updated_at = NOW() "
                   "  WHERE id = $1 RETURNING id"
                   ") "
                   "UPDATE fsync SET conflict_policy = $4 "
                   "WHERE sync_id = (SELECT id FROM updated_sync)");

    conn_->prepare("update_sync_and_rsync",
                   "WITH updated_sync AS ("
                   "  UPDATE sync SET interval = $2, enabled = $3, updated_at = NOW() "
                   "  WHERE id = $1 RETURNING id"
                   ") "
                   "UPDATE rsync SET strategy = $4, conflict_policy = $5 "
                   "WHERE sync_id = (SELECT id FROM updated_sync)");

    conn_->prepare("report_sync_started", "UPDATE sync SET last_sync_at = NOW() WHERE id = $1");

    conn_->prepare("report_sync_success",
                   "UPDATE sync SET last_success_at = NOW(), last_sync_at = NOW() WHERE id = $1");

    conn_->prepare("get_fsync_config",
                   "SELECT fs.*, s.* FROM fsync fs JOIN sync s ON s.id = fs.sync_id WHERE vault_id = $1");

    conn_->prepare("get_rsync_config",
                   "SELECT rs.*, s.* FROM rsync rs JOIN sync s ON s.id = rs.sync_id WHERE vault_id = $1");

    conn_->prepare("get_sync_config",
                   "SELECT s.*, rs.*, fs.* "
                   "FROM sync s "
                   "LEFT JOIN rsync rs ON s.id = rs.sync_id "
                   "LEFT JOIN fsync fs ON s.id = fs.sync_id "
                   "WHERE vault_id = $1");
}