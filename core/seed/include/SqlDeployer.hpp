#pragma once

#include "log/Registry.hpp"

#include <pqxx/pqxx>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <format>
#include <openssl/sha.h>

namespace vh::db::seed {

namespace fs = std::filesystem;

inline std::string readFileToString(const fs::path& p) {
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open SQL file: " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::string sha256Hex(const std::string& s) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), hash);

    static const auto* hex = "0123456789abcdef";
    std::string out;
    out.resize(SHA256_DIGEST_LENGTH * 2);
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out[i * 2 + 0] = hex[(hash[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[(hash[i] >> 0) & 0xF];
    }
    return out;
}

// Migration files that were edited in place AFTER being shipped in a released tag.
//
// SqlDeployer records sha256(raw file bytes) per migration in schema_migrations and refuses to start when the
// recorded hash differs from the file on disk. Every entry below is a hash that a released package really shipped
// (so a real database may have recorded it) for a file whose content later changed. When the recorded hash matches
// one of these, the deployer accepts it, does NOT re-execute the file, and rewrites the recorded hash to the current
// one. The schema delta introduced by each in-place edit is owned by a separate, idempotent forward migration
// (named per entry), so re-running the edited file is never needed.
//
// Rewriting the recorded hash (instead of leaving the historical one) keeps the check strict: once a database is
// reconciled it matches the current file exactly, any later drift of that file fails again, and this table only
// matters for databases that have not yet upgraded past the edit.
//
// Rules:
//  - Never edit a shipped migration. Add the next-numbered forward migration instead.
//  - This table is append-only and reviewed. It must match the "historical" lines of
//    core/seed/shipped_migrations.lock; tools/contracts/test_migration_checksums_contract.py
//    enforces that and checks every released tag's deploy/psql against the lock when tags are available.
struct HistoricalMigrationChecksum {
    std::string_view filename;
    std::string_view sha256;
    std::string_view note;
};

inline constexpr std::array kHistoricalMigrationChecksums{
    HistoricalMigrationChecksum{
        "020_vaults.sql",
        "64170212eb3f9fbe0f7c949791b953f0376a1f2bcf66b3dec7afc5db6325d570",
        "shipped v0.28.0-alpha..v1.5.0; v1.5.1 added s3.storage_tier_id in place "
        "(forward: 090_s3_storage_tier_id.sql)"
    },
    HistoricalMigrationChecksum{
        "060_acl.sql",
        "c154cd7e10f7931f29c12b9221c90f66d6b953cfb5e3dc15ea50f0940e5d4220",
        "shipped v0.28.0-alpha..v1.5.1; v1.6.0 added admin_role.s3_gateway_permissions in place "
        "(forward: 097_admin_role_s3_gateway_permissions.sql)"
    },
    HistoricalMigrationChecksum{
        "082_stats_metric_samples.sql",
        "720caaa27c0d51635fe3f459a8426f019d3f0dbdc0b7ae176d283309933a7f53",
        "shipped v1.2.4..v1.2.5 with alertable FUSE error columns; v1.2.6 reverted the file "
        "(forward: 084_stats_fuse_alertable_errors.sql)"
    },
};

inline bool isAcceptedHistoricalChecksum(const std::string_view filename, const std::string_view sha256) {
    return std::ranges::any_of(kHistoricalMigrationChecksums, [&](const HistoricalMigrationChecksum& e) {
        return e.filename == filename && e.sha256 == sha256;
    });
}

struct ReconciledMigration {
    std::string filename;
    std::string previous_sha256;
    std::string current_sha256;
};

struct SqlDeployReport {
    std::vector<std::string> applied;
    std::vector<ReconciledMigration> reconciled;
};

struct SqlDeployer {
    // Creates the migrations table if missing.
    static void ensureMigrationsTable(pqxx::work& txn) {
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS schema_migrations (
                filename   TEXT PRIMARY KEY,
                sha256     TEXT NOT NULL,
                applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
        )");
    }

    // Returns true if this exact file hash is already applied.
    static bool isApplied(pqxx::work& txn, const std::string& filename, const std::string& hash) {
        const auto r = txn.exec(
            "SELECT 1 FROM schema_migrations WHERE filename = $1 AND sha256 = $2",
            pqxx::params{filename, hash}
        );
        return !r.empty();
    }

    // Mark file applied (upsert).
    static void markApplied(pqxx::work& txn, const std::string& filename, const std::string& hash) {
        txn.exec(R"(
            INSERT INTO schema_migrations (filename, sha256)
            VALUES ($1, $2)
            ON CONFLICT (filename)
            DO UPDATE SET sha256 = EXCLUDED.sha256, applied_at = CURRENT_TIMESTAMP
        )", pqxx::params{filename, hash});
    }

    // Rewrite a reconciled migration's recorded hash. applied_at is preserved: the file was not re-applied.
    static void updateRecordedHash(pqxx::work& txn, const std::string& filename,
                                   const std::string& previous, const std::string& current) {
        txn.exec(
            "UPDATE schema_migrations SET sha256 = $3 WHERE filename = $1 AND sha256 = $2",
            pqxx::params{filename, previous, current}
        );
    }

    static bool filenameExists(pqxx::work& txn, const std::string& filename, std::string* existing_hash = nullptr) {
        const auto r = txn.exec(
            "SELECT sha256 FROM schema_migrations WHERE filename = $1",
            pqxx::params{filename}
        );
        if (r.empty()) return false;
        if (existing_hash) *existing_hash = r[0][0].as<std::string>();
        return true;
    }

    // Load *.sql from a directory, sort by filename, execute. Everything runs inside the caller's transaction, so
    // a failure anywhere rolls back every migration and schema_migrations row from this run.
    //
    // Behavior:
    //  - Not recorded yet => execute, then record its hash.
    //  - Recorded hash == file hash => skip.
    //  - Recorded hash is an accepted historical hash (kHistoricalMigrationChecksums) => skip WITHOUT executing,
    //    rewrite the recorded hash to the current one, and log it. A forward migration owns the schema delta.
    //  - Any other mismatch => throw: a shipped migration was edited. Create a new migration instead.
    static SqlDeployReport applyDir(pqxx::work& txn, const fs::path& dir) {
        if (!fs::exists(dir)) throw std::runtime_error("SQL deploy dir does not exist: " + dir.string());
        if (!fs::is_directory(dir)) throw std::runtime_error("SQL deploy path is not a directory: " + dir.string());

        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            const auto& p = e.path();
            if (p.extension() == ".sql") files.push_back(p);
        }

        std::ranges::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });

        // Until migration 100 converts them, naive `timestamp` columns hold wall time in the database's own zone, and
        // 100 reads them in that zone. Rows this run writes before 100 (schema_migrations, seed data) must land in
        // the same zone, not the daemon's UTC session (db::Connection::configureSession): run the whole deploy in it.
        // Local to this transaction; timestamptz columns are absolute either way.
        txn.exec("SELECT set_config('TimeZone', COALESCE(NULLIF(current_setting('vaulthalla.database_timezone', true), "
                 "''), current_setting('TimeZone')), true)");

        SqlDeployReport report;

        for (const auto& p : files) {
            const std::string sql = readFileToString(p);
            const std::string hash = sha256Hex(sql);
            const std::string filename = p.filename().string();

            std::string existing;
            if (filenameExists(txn, filename, &existing)) {
                if (existing == hash) continue; // exact match already applied

                if (isAcceptedHistoricalChecksum(filename, existing)) {
                    updateRecordedHash(txn, filename, existing, hash);
                    log::Registry::vaulthalla()->info(
                        "[SqlDeployer] Migration {} was recorded with a known historical checksum ({}); "
                        "accepting it without re-applying and recording the current checksum ({})",
                        filename, existing, hash
                    );
                    report.reconciled.push_back({filename, existing, hash});
                    continue;
                }

                throw std::runtime_error(std::format("Migration file was modified after being applied: {} "
                                                     "(db={} file={}). Create a new migration instead.",
                    filename, existing, hash)
                );
            }

            txn.exec(sql);
            markApplied(txn, filename, hash);
            report.applied.push_back(filename);
        }

        return report;
    }
};

} // namespace vh::database::seed
