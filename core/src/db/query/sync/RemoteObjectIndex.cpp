#include "db/query/sync/RemoteObjectIndex.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/File.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemoteObject.hpp"

using namespace vh::db::encoding;

namespace vh::db::query::sync {

uint64_t RemoteObjectIndex::countForVault(const uint32_t vaultId) {
    return Transactions::exec("RemoteObjectIndex::countForVault", [&](pqxx::work& txn) {
        return txn.exec(pqxx::prepped{"remote_object_index.count_for_vault"}, vaultId).one_field().as<uint64_t>();
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
            object.source
        };
        txn.exec(pqxx::prepped{"remote_object_index.upsert"}, params);
    });
}

void RemoteObjectIndex::deleteKey(const uint32_t vaultId, const std::filesystem::path& key) {
    Transactions::exec("RemoteObjectIndex::deleteKey", [&](pqxx::work& txn) {
        auto normalized = to_utf8_string(key.lexically_normal().u8string());
        while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
        txn.exec(pqxx::prepped{"remote_object_index.delete_key"}, pqxx::params{vaultId, normalized});
    });
}

std::optional<std::string> RemoteObjectIndex::getManifestETag(const uint32_t vaultId, const std::string& manifestKey) {
    return Transactions::exec("RemoteObjectIndex::getManifestETag", [&](pqxx::work& txn) -> std::optional<std::string> {
        const auto res = txn.exec(pqxx::prepped{"remote_manifest_state.get_etag"}, pqxx::params{vaultId, manifestKey});
        if (res.empty() || res.one_row()["etag"].is_null()) return std::nullopt;
        return res.one_row()["etag"].as<std::string>();
    });
}

void RemoteObjectIndex::upsertManifestETag(
    const uint32_t vaultId,
    const std::string& manifestKey,
    const std::optional<std::string>& etag) {
    Transactions::exec("RemoteObjectIndex::upsertManifestETag", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"remote_manifest_state.upsert"}, pqxx::params{vaultId, manifestKey, etag});
    });
}

}
