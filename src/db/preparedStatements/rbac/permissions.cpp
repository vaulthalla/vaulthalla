#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedPermissions() const {
    conn_->prepare(
        "insert_raw_permission",
        "INSERT INTO permission (name, description, category, bit_position) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (name, bit_position, category) DO UPDATE SET "
        "description = EXCLUDED.description"
    );
}
