#include "db/DBConnection.hpp"

void vh::db::Connection::initPreparedDbStats() const {
    conn_->prepare("db_stats.summary",
        R"SQL(
            SELECT
                current_database() AS database_name,
                pg_database_size(current_database()) AS db_size_bytes,
                (SELECT setting::bigint FROM pg_settings WHERE name = 'max_connections') AS connections_max,
                (
                    SELECT SUM(blks_hit)::double precision / NULLIF(SUM(blks_hit + blks_read), 0)
                    FROM pg_stat_database
                    WHERE datname = current_database()
                ) AS cache_hit_ratio,
                COALESCE((
                    SELECT deadlocks
                    FROM pg_stat_database
                    WHERE datname = current_database()
                ), 0) AS deadlocks,
                COALESCE((
                    SELECT temp_bytes
                    FROM pg_stat_database
                    WHERE datname = current_database()
                ), 0) AS temp_bytes,
                (
                    SELECT EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - MIN(xact_start)))::bigint
                    FROM pg_stat_activity
                    WHERE datname = current_database()
                      AND xact_start IS NOT NULL
                ) AS oldest_transaction_age_seconds;
        )SQL"
    );

    conn_->prepare("db_stats.connections",
        R"SQL(
            SELECT COALESCE(state, 'unknown') AS state, COUNT(*) AS count
            FROM pg_stat_activity
            WHERE datname = current_database()
            GROUP BY COALESCE(state, 'unknown');
        )SQL"
    );

    conn_->prepare("db_stats.pg_stat_statements_enabled",
        R"SQL(
            SELECT EXISTS (
                SELECT 1
                FROM pg_extension
                WHERE extname = 'pg_stat_statements'
            ) AS enabled;
        )SQL"
    );

    conn_->prepare("db_stats.largest_tables",
        R"SQL(
            SELECT
                (schemaname || '.' || relname) AS table_name,
                pg_total_relation_size(relid) AS total_bytes,
                pg_relation_size(relid) AS table_bytes,
                (pg_total_relation_size(relid) - pg_relation_size(relid)) AS index_bytes,
                GREATEST(n_live_tup, 0)::bigint AS row_estimate
            FROM pg_stat_user_tables
            ORDER BY pg_total_relation_size(relid) DESC
            LIMIT 8;
        )SQL"
    );
}
