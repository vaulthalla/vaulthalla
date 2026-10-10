#include "db/query/sync/Conflict.hpp"

#include "db/Transactions.hpp"
#include "fs/model/File.hpp"
#include "sync/model/Artifact.hpp"
#include "sync/model/Conflict.hpp"

#include <pqxx/pqxx>

#include <algorithm>
#include <stdexcept>

namespace vh::db::query::sync {

namespace {

using vh::sync::model::Baseline;

template <typename T>
std::optional<T> opt(pqxx::field_ref f) {
    if (f.is_null()) return std::nullopt;
    return f.as<T>();
}

std::optional<std::time_t> optEpoch(pqxx::field_ref f) {
    if (f.is_null()) return std::nullopt;
    return static_cast<std::time_t>(f.as<int64_t>());
}

// An INTEGER[] literal ("{1,2,3}") for = ANY($n::INTEGER[]).
std::string intArray(const std::vector<uint32_t>& ids) {
    std::string out = "{";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(ids[i]);
    }
    return out + "}";
}

std::optional<std::string> nonEmpty(const std::string& s) {
    if (s.empty()) return std::nullopt;
    return s;
}

// Every column a ConflictRecord needs; the artifacts are folded in with one LEFT JOIN per side.
constexpr auto kSelectConflict = R"SQL(
    SELECT c.id, c.vault_id, c.file_id, c.event_id, c.conflict_type, c.resolution,
           e.path AS file_path, e.name AS file_name,
           EXTRACT(EPOCH FROM c.created_at)::BIGINT AS created_epoch,
           EXTRACT(EPOCH FROM COALESCE(c.updated_at, c.created_at))::BIGINT AS updated_epoch,
           EXTRACT(EPOCH FROM c.resolved_at)::BIGINT AS resolved_epoch,
           l.size_bytes AS l_size, l.mime_type AS l_mime, l.content_hash AS l_hash,
           EXTRACT(EPOCH FROM l.last_modified)::BIGINT AS l_modified, l.remote_etag AS l_etag, l.encrypted AS l_encrypted,
           l.encryption_iv AS l_iv, l.key_version AS l_key_version,
           r.size_bytes AS r_size, r.mime_type AS r_mime, r.content_hash AS r_hash,
           EXTRACT(EPOCH FROM r.last_modified)::BIGINT AS r_modified, r.remote_etag AS r_etag, r.encrypted AS r_encrypted,
           r.encryption_iv AS r_iv, r.key_version AS r_key_version
    FROM sync_conflicts c
    LEFT JOIN fs_entry e ON e.id = c.file_id
    LEFT JOIN sync_conflict_artifacts l ON l.conflict_id = c.id AND l.side = 'local'
    LEFT JOIN sync_conflict_artifacts r ON r.conflict_id = c.id AND r.side = 'upstream'
)SQL";

ConflictSide sideFromRow(pqxx::row_ref row, const std::string& p) {
    ConflictSide s;
    s.size_bytes = opt<int64_t>(row[p + "_size"]).value_or(0);
    s.mime_type = opt<std::string>(row[p + "_mime"]);
    s.content_hash = opt<std::string>(row[p + "_hash"]);
    s.modified_at = optEpoch(row[p + "_modified"]);
    s.etag = opt<std::string>(row[p + "_etag"]);
    s.encrypted = opt<bool>(row[p + "_encrypted"]);
    s.encryption_iv = opt<std::string>(row[p + "_iv"]);
    s.key_version = opt<unsigned int>(row[p + "_key_version"]);
    return s;
}

ConflictRecord recordFromRow(pqxx::row_ref row) {
    ConflictRecord r;
    r.id = row["id"].as<uint32_t>();
    r.vault_id = opt<uint32_t>(row["vault_id"]).value_or(0);
    r.file_id = row["file_id"].as<uint32_t>();
    r.event_id = opt<uint32_t>(row["event_id"]);
    r.path = opt<std::string>(row["file_path"]).value_or("");
    r.name = opt<std::string>(row["file_name"]).value_or("");
    r.type = row["conflict_type"].as<std::string>();
    r.resolution = opt<std::string>(row["resolution"]).value_or("unresolved");
    r.created_at = optEpoch(row["created_epoch"]).value_or(0);
    r.updated_at = optEpoch(row["updated_epoch"]).value_or(r.created_at);
    r.resolved_at = optEpoch(row["resolved_epoch"]);
    r.local = sideFromRow(row, "l");
    r.remote = sideFromRow(row, "r");
    return r;
}

void attachReasons(pqxx::work& txn, std::vector<ConflictRecord>& records) {
    if (records.empty()) return;
    std::unordered_map<uint32_t, ConflictRecord*> byId;
    std::vector<uint32_t> ids;
    for (auto& r : records) {
        byId[r.id] = &r;
        ids.push_back(r.id);
    }
    const auto res = txn.exec(
        "SELECT conflict_id, reason_code, COALESCE(reason_message, '') AS reason_message "
        "FROM sync_conflict_reasons WHERE conflict_id = ANY($1::INTEGER[]) ORDER BY id",
        pqxx::params{intArray(ids)});
    for (const auto& row : res)
        if (const auto it = byId.find(row["conflict_id"].as<uint32_t>()); it != byId.end())
            it->second->reasons.push_back({row["reason_code"].as<std::string>(), row["reason_message"].as<std::string>()});
}

void upsertBaselineTxn(pqxx::work& txn, const uint32_t vaultId, const Baseline& b) {
    txn.exec(R"SQL(
        INSERT INTO sync_file_baseline (file_id, vault_id, content_hash, size_bytes, remote_etag, remote_size_bytes,
                                        remote_content_hash, synced_at)
        SELECT $1, $2, $3, $4, $5, $6, $7, NOW()
        WHERE EXISTS (SELECT 1 FROM files WHERE fs_entry_id = $1)
        ON CONFLICT (file_id) DO UPDATE SET
            vault_id = EXCLUDED.vault_id,
            content_hash = EXCLUDED.content_hash,
            size_bytes = EXCLUDED.size_bytes,
            remote_content_hash = EXCLUDED.remote_content_hash,
            remote_etag = EXCLUDED.remote_etag,
            remote_size_bytes = EXCLUDED.remote_size_bytes,
            synced_at = NOW()
    )SQL", pqxx::params{b.file_id, vaultId, b.content_hash, static_cast<int64_t>(b.size_bytes), b.remote_etag,
                        b.remote_size_bytes ? std::make_optional(static_cast<int64_t>(*b.remote_size_bytes))
                                            : std::optional<int64_t>{},
                        b.remote_content_hash});
}

void writeArtifact(pqxx::work& txn, const uint32_t conflictId, const vh::sync::model::Artifact& artifact,
                   const bool remote) {
    const auto& f = artifact.file;
    if (!f) return;
    txn.exec(R"SQL(
        INSERT INTO sync_conflict_artifacts
            (conflict_id, side, size_bytes, mime_type, content_hash, last_modified, encryption_iv, key_version,
             local_backing_path, remote_etag, encrypted)
        VALUES ($1, $2, $3, $4, $5, CASE WHEN $6::BIGINT IS NULL THEN NULL ELSE to_timestamp($6::BIGINT) END,
                $7, $8, $9, $10, $11)
        ON CONFLICT (conflict_id, side) DO UPDATE SET
            size_bytes = EXCLUDED.size_bytes,
            mime_type = EXCLUDED.mime_type,
            content_hash = EXCLUDED.content_hash,
            last_modified = EXCLUDED.last_modified,
            encryption_iv = EXCLUDED.encryption_iv,
            key_version = EXCLUDED.key_version,
            local_backing_path = EXCLUDED.local_backing_path,
            remote_etag = EXCLUDED.remote_etag,
            encrypted = EXCLUDED.encrypted
    )SQL", pqxx::params{
        conflictId, artifact.sideToString(), static_cast<int64_t>(f->size_bytes), f->mime_type, f->content_hash,
        f->updated_at ? std::make_optional(static_cast<int64_t>(f->updated_at)) : std::optional<int64_t>{},
        nonEmpty(f->encryption_iv),
        f->encrypted_with_key_version ? std::make_optional(f->encrypted_with_key_version) : std::optional<unsigned int>{},
        remote || f->backing_path.empty() ? std::optional<std::string>{} : std::make_optional(f->backing_path.string()),
        remote ? f->remote_etag : std::optional<std::string>{},
        remote ? f->remote_encrypted : std::optional<bool>{}
    });
}

}

std::unordered_map<uint32_t, Baseline> Conflict::baselinesForVault(const uint32_t vaultId) {
    return Transactions::exec("SyncConflict::baselinesForVault", [&](pqxx::work& txn) {
        std::unordered_map<uint32_t, Baseline> out;
        const auto res = txn.exec(
            "SELECT file_id, content_hash, size_bytes, remote_etag, remote_size_bytes, remote_content_hash "
            "FROM sync_file_baseline WHERE vault_id = $1", pqxx::params{vaultId});
        out.reserve(res.size());
        for (const auto& row : res) {
            Baseline b;
            b.file_id = row["file_id"].as<uint32_t>();
            b.content_hash = opt<std::string>(row["content_hash"]);
            b.size_bytes = static_cast<uint64_t>(row["size_bytes"].as<int64_t>());
            b.remote_etag = opt<std::string>(row["remote_etag"]);
            b.remote_content_hash = opt<std::string>(row["remote_content_hash"]);
            if (const auto size = opt<int64_t>(row["remote_size_bytes"])) b.remote_size_bytes = static_cast<uint64_t>(*size);
            out.emplace(b.file_id, std::move(b));
        }
        return out;
    });
}

void Conflict::upsertBaseline(const uint32_t vaultId, const Baseline& baseline) {
    Transactions::exec("SyncConflict::upsertBaseline", [&](pqxx::work& txn) { upsertBaselineTxn(txn, vaultId, baseline); });
}

std::unordered_map<uint32_t, ConflictRecord> Conflict::openForVault(const uint32_t vaultId) {
    return Transactions::exec("SyncConflict::openForVault", [&](pqxx::work& txn) {
        auto rows = txn.exec(std::string(kSelectConflict) + " WHERE c.vault_id = $1 AND c.resolution = 'unresolved'",
                             pqxx::params{vaultId});
        std::vector<ConflictRecord> records;
        records.reserve(rows.size());
        for (const auto& row : rows) records.push_back(recordFromRow(row));
        attachReasons(txn, records);
        std::unordered_map<uint32_t, ConflictRecord> out;
        for (auto& r : records) out.emplace(r.file_id, std::move(r));
        return out;
    });
}

std::vector<ConflictRecord> Conflict::listOpen(const std::optional<std::vector<uint32_t>>& vaultIds) {
    return Transactions::exec("SyncConflict::listOpen", [&](pqxx::work& txn) {
        std::vector<ConflictRecord> records;
        if (vaultIds && vaultIds->empty()) return records;
        const auto rows = txn.exec(
            std::string(kSelectConflict) +
                " JOIN vault v ON v.id = c.vault_id AND v.deleted_at IS NULL"
                " WHERE c.resolution = 'unresolved' AND ($1::INTEGER[] IS NULL OR c.vault_id = ANY($1::INTEGER[]))"
                " ORDER BY c.created_at DESC, c.id DESC",
            pqxx::params{vaultIds ? std::make_optional(intArray(*vaultIds)) : std::optional<std::string>{}});
        records.reserve(rows.size());
        for (const auto& row : rows) records.push_back(recordFromRow(row));
        attachReasons(txn, records);
        return records;
    });
}

std::vector<std::pair<uint32_t, uint64_t>> Conflict::openCountsByVault() {
    return Transactions::exec("SyncConflict::openCountsByVault", [&](pqxx::work& txn) {
        std::vector<std::pair<uint32_t, uint64_t>> out;
        const auto res = txn.exec(
            "SELECT c.vault_id, COUNT(*) AS n FROM sync_conflicts c "
            "JOIN vault v ON v.id = c.vault_id AND v.deleted_at IS NULL "
            "WHERE c.resolution = 'unresolved' GROUP BY c.vault_id ORDER BY c.vault_id");
        for (const auto& row : res) out.emplace_back(row["vault_id"].as<uint32_t>(), row["n"].as<uint64_t>());
        return out;
    });
}

std::optional<ConflictRecord> Conflict::get(const uint32_t id) {
    return Transactions::exec("SyncConflict::get", [&](pqxx::work& txn) -> std::optional<ConflictRecord> {
        const auto rows = txn.exec(std::string(kSelectConflict) + " WHERE c.id = $1", pqxx::params{id});
        if (rows.empty()) return std::nullopt;
        std::vector<ConflictRecord> records{recordFromRow(rows[0])};
        attachReasons(txn, records);
        return records.front();
    });
}

void Conflict::applyPass(const uint32_t vaultId, const std::optional<uint32_t> eventId, const ConflictPassWrites& writes) {
    if (writes.empty()) return;

    // Short transactions: the first pass over a large vault records a baseline per file, and a pool slot must never
    // be held for the whole batch (FUSE needs the pool too). Each row stands on its own.
    constexpr std::size_t kBaselineBatch = 500;
    for (std::size_t i = 0; i < writes.baselines.size(); i += kBaselineBatch)
        Transactions::exec("SyncConflict::applyPass.baselines", [&](pqxx::work& txn) {
            const auto end = std::min(writes.baselines.size(), i + kBaselineBatch);
            for (std::size_t j = i; j < end; ++j) upsertBaselineTxn(txn, vaultId, writes.baselines[j]);
        });

    for (std::size_t i = 0; i < writes.close.size(); i += kBaselineBatch)
        Transactions::exec("SyncConflict::applyPass.close", [&](pqxx::work& txn) {
            const auto end = std::min(writes.close.size(), i + kBaselineBatch);
            for (std::size_t j = i; j < end; ++j)
                txn.exec("UPDATE sync_conflicts SET resolution = $2, resolved_at = NOW(), updated_at = NOW() "
                         "WHERE file_id = $1 AND resolution = 'unresolved'",
                         pqxx::params{writes.close[j].first, writes.close[j].second});
        });

    // A conflict (row, both artifacts, reasons) is written whole; a few of them per transaction.
    constexpr std::size_t kConflictBatch = 50;
    for (std::size_t i = 0; i < writes.open.size(); i += kConflictBatch)
        Transactions::exec("SyncConflict::applyPass.open", [&](pqxx::work& txn) {
            const auto end = std::min(writes.open.size(), i + kConflictBatch);
            for (std::size_t j = i; j < end; ++j) {
                const auto& c = writes.open[j];
                if (!c) continue;
                // event_id only on insert: it is the run that first saw the conflict. A sync_event row that no longer
                // exists (the event is pruned or was never saved) records NULL instead of failing the pass.
                const auto id = txn.exec(R"SQL(
                    INSERT INTO sync_conflicts (event_id, file_id, vault_id, conflict_type, resolution, created_at, updated_at)
                    VALUES ((SELECT id FROM sync_event WHERE id = $1), $2, $3, $4, 'unresolved', NOW(), NOW())
                    ON CONFLICT (file_id) WHERE resolution = 'unresolved'
                    DO UPDATE SET conflict_type = EXCLUDED.conflict_type,
                                  vault_id = EXCLUDED.vault_id,
                                  updated_at = NOW()
                    RETURNING id
                )SQL", pqxx::params{eventId, c->file_id, vaultId, c->typeToString()}).one_field_ref().as<uint32_t>();
                c->id = id;

                writeArtifact(txn, id, c->artifacts.local, false);
                writeArtifact(txn, id, c->artifacts.upstream, true);

                txn.exec("DELETE FROM sync_conflict_reasons WHERE conflict_id = $1", pqxx::params{id});
                for (auto& reason : c->reasons) {
                    reason.conflict_id = id;
                    reason.id = txn.exec(
                        "INSERT INTO sync_conflict_reasons (conflict_id, reason_code, reason_message) VALUES ($1, $2, $3) "
                        "ON CONFLICT (conflict_id, reason_code) DO UPDATE SET reason_message = EXCLUDED.reason_message "
                        "RETURNING id",
                        pqxx::params{id, reason.code, reason.message}).one_field_ref().as<uint32_t>();
                }
            }
        });
}

bool Conflict::finishResolution(const uint32_t conflictId, const std::string& resolution,
                                const std::optional<Baseline>& baseline, const uint32_t vaultId) {
    return Transactions::exec("SyncConflict::finishResolution", [&](pqxx::work& txn) {
        const auto closed = txn.exec(
            "UPDATE sync_conflicts SET resolution = $2, resolved_at = NOW(), updated_at = NOW() "
            "WHERE id = $1 AND resolution = 'unresolved'",
            pqxx::params{conflictId, resolution});
        if (closed.affected_rows() == 0) return false;
        if (baseline) upsertBaselineTxn(txn, vaultId, *baseline);
        return true;
    });
}

ConflictSide sideFrom(const vh::sync::model::Conflict& conflict, const bool local) {
    const auto& file = local ? conflict.artifacts.local.file : conflict.artifacts.upstream.file;
    ConflictSide s;
    if (!file) return s;
    s.size_bytes = file->size_bytes;
    s.mime_type = file->mime_type;
    s.content_hash = file->content_hash;
    if (file->updated_at) s.modified_at = file->updated_at;
    if (!local) {
        s.etag = file->remote_etag;
        s.encrypted = file->remote_encrypted;
    }
    s.encryption_iv = nonEmpty(file->encryption_iv);
    if (file->encrypted_with_key_version) s.key_version = file->encrypted_with_key_version;
    return s;
}

bool sideMatches(const ConflictSide& side, const std::optional<std::string>& hash, const uint64_t size,
                 const std::optional<std::string>& etag) {
    if (side.content_hash && hash) return *side.content_hash == *hash && side.size_bytes == size;
    if (side.etag && etag) return *side.etag == *etag;
    return side.size_bytes == size && side.content_hash == hash;
}

}
