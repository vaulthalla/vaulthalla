#pragma once

#include <pqxx/pqxx>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

    static bool filenameExists(pqxx::work& txn, const std::string& filename, std::string* existing_hash = nullptr) {
        const auto r = txn.exec(
            "SELECT sha256 FROM schema_migrations WHERE filename = $1",
            pqxx::params{filename}
        );
        if (r.empty()) return false;
        if (existing_hash) *existing_hash = r[0][0].as<std::string>();
        return true;
    }

    // Load *.sql from a directory, sort by filename, execute.
    //
    // Behavior:
    //  - If file content hash matches what was applied last => skip.
    //  - If file content changed => execute again, then update schema_migrations.
    //
    // WARNING: re-executing “schema” files is only safe if the SQL is idempotent.
    static void applyDir(pqxx::work& txn, const fs::path& dir) {
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

        for (const auto& p : files) {
            const std::string sql = readFileToString(p);
            const std::string hash = sha256Hex(sql);
            const std::string filename = p.filename().string();

            std::string existing;
            if (filenameExists(txn, filename, &existing)) {
                if (existing != hash) {
                    throw std::runtime_error(std::format("Migration file was modified after being applied: {} "
                                                         "(db={} file={}). Create a new migration instead.",
                        filename, existing, hash)
                    );
                }
                continue; // exact match already applied
            }

            txn.exec(sql);
            markApplied(txn, filename, hash);
        }
    }

    // Optional: apply “migrations only once” semantics by using a separate table,
    // or by never reapplying changed files (needs implementation if so)
};

} // namespace vh::database::seed
