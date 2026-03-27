#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedOperations() const {
    conn_->prepare("insert_operation",
                   "INSERT INTO operations (fs_entry_id, executed_by, operation, target, status, "
                   "source_path, destination_path) "
                   "VALUES ($1, $2, $3, $4, $5, $6, $7) ");

    conn_->prepare("get_pending_operations",
                   "SELECT * FROM operations WHERE status = 'pending' AND fs_entry_id = $1");

    conn_->prepare("list_pending_operations_by_vault",
                   "SELECT * FROM operations WHERE status = 'pending' AND fs_entry_id IN "
                   "(SELECT id FROM fs_entry WHERE vault_id = $1)");

    conn_->prepare("mark_operation_completed_and_update",
                   "UPDATE operations SET status = $2, completed_at = NOW(), error = $3 WHERE id = $1");

    conn_->prepare("delete_operation", "DELETE FROM operations WHERE id = $1");
}
