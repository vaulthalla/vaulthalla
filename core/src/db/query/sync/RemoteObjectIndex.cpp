#include "db/query/sync/RemoteObjectIndex.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/File.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemoteObject.hpp"

#include <algorithm>
#include <cctype>

using namespace vh::db::encoding;

namespace vh::db::query::sync {

namespace {
    std::string normalizedSequencer(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        while (!value.empty() && value.front() == '0') value.erase(value.begin());
        return value;
    }
}

bool s3SequencerIsNewerOrEqual(
    const std::optional<std::string>& candidate,
    const std::optional<std::string>& current) {
    if (!candidate || candidate->empty()) return true;
    if (!current || current->empty()) return true;

    const auto next = normalizedSequencer(*candidate);
    const auto existing = normalizedSequencer(*current);
    if (next.size() != existing.size()) return next.size() > existing.size();
    return next >= existing;
}

bool RemoteIndexSummary::isStale(const std::optional<std::chrono::seconds> maxAge, const std::time_t now) const noexcept {
    if (!maxAge || !indexed_at) return false;
    return now > *indexed_at && now - *indexed_at > maxAge->count();
}

uint64_t RemoteObjectIndex::countForVault(const uint32_t vaultId) {
    return Transactions::exec("RemoteObjectIndex::countForVault", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"remote_object_index.count_for_vault"}, vaultId).one_field().as<uint64_t>();
    });
}

RemoteIndexSummary RemoteObjectIndex::summaryForVault(const uint32_t vaultId, const std::string& manifestKey) {
    return Transactions::exec("RemoteObjectIndex::summaryForVault", [&](pqxx::work& txn) {
        RemoteIndexSummary summary;

        const auto indexRes = txn.exec(pqxx::prepped{"remote_object_index.summary_for_vault"}, vaultId);
        if (!indexRes.empty()) {
            const auto row = indexRes.one_row();
            summary.object_count = row["object_count"].as<uint64_t>();
            if (!row["source"].is_null()) summary.source = row["source"].as<std::string>();
            if (!row["indexed_at"].is_null()) summary.indexed_at = parsePostgresTimestamp(row["indexed_at"].as<std::string>());
        }

        const auto manifestRes = txn.exec(pqxx::prepped{"remote_manifest_state.get"}, pqxx::params{vaultId, manifestKey});
        if (!manifestRes.empty()) {
            const auto row = manifestRes.one_row();
            if (!row["etag"].is_null()) summary.manifest_etag = row["etag"].as<std::string>();
            if (!row["updated_at"].is_null()) summary.manifest_updated_at = parsePostgresTimestamp(row["updated_at"].as<std::string>());
            if (!row["generated_at"].is_null()) summary.manifest_generated_at = parsePostgresTimestamp(row["generated_at"].as<std::string>());
            if (!row["object_count"].is_null()) summary.manifest_object_count = row["object_count"].as<uint64_t>();
            if (!row["object_checksum"].is_null()) summary.manifest_object_checksum = row["object_checksum"].as<std::string>();
        }

        return summary;
    });
}

std::vector<RemoteObjectIndex::FilePtr> RemoteObjectIndex::listFilesForVault(const uint32_t vaultId) {
    return Transactions::exec("RemoteObjectIndex::listFilesForVault", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"remote_object_index.list_for_vault"}, vaultId);
        std::vector<FilePtr> files;
        files.reserve(res.size());
        for (const auto& row : res)
            files.push_back(vh::sync::model::RemoteObject(row).toFile());
        return files;
    });
}

void RemoteObjectIndex::replaceFromListObjects(const uint32_t vaultId, const std::vector<FilePtr>& files) {
    replace(vaultId, files, "list_objects_v2");
}

void RemoteObjectIndex::replaceFromManifest(const uint32_t vaultId, const std::vector<FilePtr>& files) {
    replace(vaultId, files, "manifest");
}

void RemoteObjectIndex::replace(const uint32_t vaultId, const std::vector<FilePtr>& files, const std::string& source) {
    Transactions::exec("RemoteObjectIndex::replace", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"remote_object_index.delete_for_vault"}, vaultId);

        for (const auto& file : files) {
            if (!file) continue;
            if (vh::sync::model::remote_manifest::isVaulthallaManifestKey(file->path)) continue;
            const vh::sync::model::RemoteObject object(vaultId, file, source);
            const pqxx::params params{
                object.vault_id,
                to_utf8_string(object.object_key.u8string()),
                object.size_bytes,
                timestampToString(object.last_modified),
                object.etag,
                object.storage_class,
                object.restore_status,
                object.version_id,
                object.event_sequencer,
                object.source
            };
            txn.exec(pqxx::prepped{"remote_object_index.upsert"}, params);
        }
    });
}

void RemoteObjectIndex::upsertFile(const uint32_t vaultId, const FilePtr& file, const std::string& source) {
    if (!file || vh::sync::model::remote_manifest::isVaulthallaManifestKey(file->path)) return;

    Transactions::exec("RemoteObjectIndex::upsertFile", [&](pqxx::work& txn) {
        const vh::sync::model::RemoteObject object(vaultId, file, source);
        const pqxx::params params{
            object.vault_id,
            to_utf8_string(object.object_key.u8string()),
            object.size_bytes,
            timestampToString(object.last_modified),
            object.etag,
            object.storage_class,
            object.restore_status,
            object.version_id,
            object.event_sequencer,
            object.source
        };
        txn.exec(pqxx::prepped{"remote_object_index.upsert"}, params);
    });
}

void RemoteObjectIndex::upsertEventFile(const uint32_t vaultId, const FilePtr& file) {
    upsertFile(vaultId, file, "event");
}

void RemoteObjectIndex::deleteKey(const uint32_t vaultId, const std::filesystem::path& key) {
    Transactions::exec("RemoteObjectIndex::deleteKey", [&](pqxx::work& txn) {
        auto normalized = to_utf8_string(key.lexically_normal().u8string());
        while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
        txn.exec(pqxx::prepped{"remote_object_index.delete_key"}, pqxx::params{vaultId, normalized});
    });
}

void RemoteObjectIndex::deleteEventKey(
    const uint32_t vaultId,
    const std::filesystem::path& key,
    const std::optional<std::string>& sequencer) {
    Transactions::exec("RemoteObjectIndex::deleteEventKey", [&](pqxx::work& txn) {
        auto normalized = to_utf8_string(key.lexically_normal().u8string());
        while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
        txn.exec(
            pqxx::prepped{"remote_object_index.delete_key_if_not_stale_event"},
            pqxx::params{vaultId, normalized, sequencer});
    });
}

std::optional<std::string> RemoteObjectIndex::getManifestETag(const uint32_t vaultId, const std::string& manifestKey) {
    return Transactions::exec("RemoteObjectIndex::getManifestETag", [&](pqxx::work& txn) -> std::optional<std::string> {
        const auto res = txn.exec(pqxx::prepped{"remote_manifest_state.get_etag"}, pqxx::params{vaultId, manifestKey});
        if (res.empty() || res.one_row()["etag"].is_null()) return std::nullopt;
        return res.one_row()["etag"].as<std::string>();
    });
}

void RemoteObjectIndex::upsertManifestState(
    const uint32_t vaultId,
    const std::string& manifestKey,
    const std::optional<std::string>& etag,
    const std::optional<std::time_t> generatedAt,
    const std::optional<uint64_t> objectCount,
    const std::optional<std::string>& objectChecksum) {
    Transactions::exec("RemoteObjectIndex::upsertManifestState", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"remote_manifest_state.upsert"},
            pqxx::params{
                vaultId,
                manifestKey,
                etag,
                generatedAt ? std::make_optional(timestampToString(*generatedAt)) : std::optional<std::string>{},
                objectCount,
                objectChecksum
            });
    });
}

void RemoteObjectIndex::upsertManifestETag(
    const uint32_t vaultId,
    const std::string& manifestKey,
    const std::optional<std::string>& etag) {
    upsertManifestState(vaultId, manifestKey, etag);
}

}
