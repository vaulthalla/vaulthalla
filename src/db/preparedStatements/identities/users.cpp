#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedUsers() const {
    conn_->prepare("insert_user",
                   "INSERT INTO users (name, email, password_hash, is_active, linux_uid, last_modified_by) "
                   "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id");

    conn_->prepare("get_user", "SELECT * FROM users WHERE id = $1");

    conn_->prepare("get_user_by_name", "SELECT * FROM users WHERE name = $1");

    conn_->prepare("user_exists", "SELECT EXISTS(SELECT 1 FROM users WHERE name = $1) AS exists");

    conn_->prepare("update_user",
                   "UPDATE users SET name = $2, email = $3, password_hash = $4, is_active = $5, linux_uid = $6, "
                   "last_modified_by = $7, updated_at = NOW() "
                   "WHERE id = $1");

    conn_->prepare("delete_user", "DELETE FROM users WHERE id = $1");

    conn_->prepare("update_user_password", "UPDATE users SET password_hash = $2 WHERE id = $1");

    conn_->prepare("update_user_last_login", "UPDATE users SET last_login = NOW() WHERE id = $1");

    conn_->prepare("get_user_id_by_linux_uid", "SELECT id FROM users WHERE linux_uid = $1");

    conn_->prepare("get_user_by_linux_uid", "SELECT * FROM users WHERE linux_uid = $1");

    conn_->prepare("admin_user_exists", "SELECT EXISTS(SELECT 1 FROM users WHERE name = 'admin') AS exists");

    conn_->prepare("get_admin_password", "SELECT password_hash FROM users WHERE name = 'admin'");
}
