#include "db/DBConnection.hpp"

void vh::db::DBConnection::initPreparedAPIKeys() const {

    conn_->prepare("get_api_key", "SELECT * FROM api_keys WHERE id = $1");

    conn_->prepare("get_api_key_by_name", "SELECT * FROM api_keys WHERE name = $1");

    conn_->prepare("upsert_api_key",
                   "INSERT INTO api_keys (user_id, name, provider, access_key, "
                   "encrypted_secret_access_key, iv, region, endpoint) "
                   "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
                   "ON CONFLICT (user_id, name, access_key) DO UPDATE SET "
                   "  provider = EXCLUDED.provider, "
                   "  encrypted_secret_access_key = EXCLUDED.encrypted_secret_access_key, "
                   "  iv = EXCLUDED.iv, "
                   "  region = EXCLUDED.region, "
                   "  endpoint = EXCLUDED.endpoint, "
                   "  created_at = CURRENT_TIMESTAMP "
                   "RETURNING id");

    conn_->prepare("remove_api_key",
                   "DELETE FROM api_keys WHERE id = $1");

    conn_->prepare("get_api_key_owner",
                   R"(SELECT u.*
FROM api_keys ak
JOIN users u ON ak.user_id = u.id
WHERE ak.id = $1)");

}
