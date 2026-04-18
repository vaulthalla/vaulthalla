#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedWaivers() const {
    conn_->prepare("insert_cloud_encryption_waiver",
                   "INSERT INTO cloud_encryption_waivers ("
                   " vault_id, user_id, api_key_id, "
                   " vault_name, user_name, user_email, linux_uid, "
                   " bucket, api_key_name, api_provider, api_access_key, api_key_region, api_key_endpoint, "
                   " encrypt_upstream, waiver_text"
                   ") "
                   "SELECT "
                   " v.id, u.id, a.id, "
                   " v.name, u.name, u.email, u.linux_uid, "
                   " s.bucket, a.name, a.provider, a.access_key, a.region, a.endpoint, "
                   " s.encrypt_upstream, $4 "
                   "FROM vault v "
                   "JOIN s3 s ON s.vault_id = v.id "
                   "JOIN api_keys a ON s.api_key_id = a.id "
                   "JOIN users u ON v.owner_id = u.id "
                   "WHERE v.id = $1 AND u.id = $2 AND a.id = $3 RETURNING id");

    conn_->prepare("insert_waiver_owner_override",
                   "INSERT INTO waiver_owner_overrides "
                   "(waiver_id, owner_id, owner_name, owner_email, overriding_role_id, overriding_role_name, "
                   "overriding_role_scope) "
                   "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING waiver_id");

    conn_->prepare("insert_permission_snapshots_for_waiver",
                   "INSERT INTO waiver_permission_snapshots ("
                   " waiver_id, permission_id, permission_name, permission_category, permission_bit_position"
                   ") "
                   "SELECT "
                   " $1, p.id, p.name, p.category, p.bit_position "
                   "FROM permission p "
                   "WHERE p.category = $2"
        );
}
