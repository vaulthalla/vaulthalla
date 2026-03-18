#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedPermissions() const {
    conn_->prepare("insert_raw_permission",
                   "INSERT INTO permission (bit_position, name, description, category) "
                   "VALUES ($1, $2, $3, $4)");
}
