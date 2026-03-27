#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedCache() const {
    conn_->prepare("upsert_cache_index",
                   "INSERT INTO cache_index (vault_id, file_id, path, type, size) "
                   "VALUES ($1, $2, $3, $4, $5) "
                   "ON CONFLICT (vault_id, path, type) DO UPDATE "
                   "SET type = EXCLUDED.type, "
                   "    size = EXCLUDED.size, "
                   "    last_accessed = CURRENT_TIMESTAMP");

    conn_->prepare("update_cache_index",
                   "UPDATE cache_index SET path = $2, type = $3, size = $4, last_accessed = NOW() WHERE id = $1");

    conn_->prepare("get_cache_index", "SELECT * FROM cache_index WHERE id = $1");

    conn_->prepare("get_cache_index_by_path", "SELECT * FROM cache_index WHERE vault_id = $1 AND path = $2");

    conn_->prepare("delete_cache_index", "DELETE FROM cache_index WHERE id = $1");

    conn_->prepare("delete_cache_index_by_path", "DELETE FROM cache_index WHERE vault_id = $1 AND path = $2");

    conn_->prepare("list_cache_indices", "SELECT * FROM cache_index WHERE vault_id = $1");

    conn_->prepare("list_cache_indices_by_path_recursive",
                   "SELECT * FROM cache_index WHERE vault_id = $1 AND path LIKE $2");

    conn_->prepare("list_cache_indices_by_path",
                   "SELECT * FROM cache_index WHERE vault_id = $1 AND path LIKE $2 AND path NOT LIKE $3");

    conn_->prepare("list_cache_indices_by_type",
                   "SELECT * FROM cache_index WHERE vault_id = $1 AND type = $2");

    conn_->prepare("list_cache_indices_by_file", "SELECT * FROM cache_index WHERE file_id = $1");

    conn_->prepare("n_largest_cache_indices",
                   "SELECT * FROM cache_index WHERE vault_id = $1 ORDER BY size DESC LIMIT $2");

    conn_->prepare("n_largest_cache_indices_by_path",
                   "SELECT * FROM cache_index WHERE vault_id = $1 "
                   "AND path LIKE $2 AND path NOT LIKE $3 ORDER BY size DESC LIMIT $4");

    conn_->prepare("n_largest_cache_indices_by_path_recursive",
                   "SELECT * FROM cache_index WHERE vault_id = $1 AND path LIKE $2 ORDER BY size DESC LIMIT $3");

    conn_->prepare("n_largest_cache_indices_by_type",
                   "SELECT * FROM cache_index WHERE vault_id = $1 AND type = $2 ORDER BY size DESC LIMIT $3");

    conn_->prepare("cache_index_exists",
                   "SELECT EXISTS (SELECT 1 FROM cache_index WHERE vault_id = $1 AND path = $2)");

    conn_->prepare("count_cache_indices", "SELECT COUNT(*) FROM cache_index WHERE vault_id = $1");

    conn_->prepare("count_cache_indices_by_type", "SELECT COUNT(*) FROM cache_index WHERE vault_id = $1 AND type = $2");
}
