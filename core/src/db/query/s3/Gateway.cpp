#include "db/query/s3/Gateway.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"

#include <algorithm>
#include <ctime>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <set>
#include <sstream>

namespace vh::db::query::s3 {

namespace {
using vh::db::encoding::from_hex_bytea;
using vh::db::encoding::parsePostgresTimestamp;
using vh::db::encoding::to_hex_bytea;

std::time_t ts(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return 0;
    return parsePostgresTimestamp(row[column].as<std::string>());
}

std::optional<std::time_t> optionalTs(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return parsePostgresTimestamp(row[column].as<std::string>());
}

GatewayCredential credentialFromRow(const pqxx::row& row) {
    const auto userId = row["user_id"].as<uint32_t>();
    return {
        .id = row["id"].as<uint32_t>(),
        .user_id = userId,
        .created_by = row["created_by"].as<std::optional<uint32_t>>(),
        .principal_user_id = row["principal_user_id"].is_null()
            ? userId
            : row["principal_user_id"].as<uint32_t>(),
        .name = row["name"].as<std::string>(),
        .access_key = row["access_key"].as<std::string>(),
        .encrypted_secret_access_key = from_hex_bytea(row["encrypted_secret_access_key"].as<std::string>()),
        .iv = from_hex_bytea(row["iv"].as<std::string>()),
        .enabled = row["enabled"].as<bool>(),
        .enforce_budget_for_local_requests = row["enforce_budget_for_local_requests"].as<bool>(),
        .scope_mode = row["scope_mode"].as<std::string>(),
        .description = row["description"].as<std::optional<std::string>>(),
        .created_at = ts(row, "created_at"),
        .last_used_at = optionalTs(row, "last_used_at"),
        .expires_at = optionalTs(row, "expires_at")
    };
}

CredentialVaultScope scopeFromRow(const pqxx::row& row) {
    return {
        .credential_id = row["credential_id"].as<uint32_t>(),
        .vault_id = row["vault_id"].as<uint32_t>(),
        .can_list = row["can_list"].as<bool>(),
        .can_read = row["can_read"].as<bool>(),
        .can_write = row["can_write"].as<bool>(),
        .can_delete = row["can_delete"].as<bool>(),
        .can_admin = row["can_admin"].as<bool>()
    };
}

BucketBinding bucketFromRow(const pqxx::row& row) {
    return {
        .vault_id = row["vault_id"].as<uint32_t>(),
        .bucket_name = row["bucket_name"].as<std::string>(),
        .api_exclusive = row["api_exclusive"].as<bool>(),
        .mode = row["mode"].as<std::string>(),
        .created_by = row["created_by"].as<std::optional<uint32_t>>(),
        .created_at = ts(row, "created_at"),
        .updated_at = ts(row, "updated_at")
    };
}

ObjectState objectFromRow(const pqxx::row& row) {
    return {
        .vault_id = row["vault_id"].as<uint32_t>(),
        .object_key = row["object_key"].as<std::string>(),
        .etag = row["etag"].as<std::string>(),
        .size_bytes = row["size_bytes"].as<uint64_t>(),
        .content_type = row["content_type"].as<std::optional<std::string>>(),
        .storage_class = row["storage_class"].as<std::optional<std::string>>(),
        .last_modified = ts(row, "last_modified"),
        .multipart = row["multipart"].as<bool>(),
        .part_count = row["part_count"].as<std::optional<uint32_t>>()
    };
}

std::map<std::string, std::string> metadataFromJson(const std::string& raw) {
    std::map<std::string, std::string> out;
    if (raw.empty()) return out;
    const auto parsed = nlohmann::json::parse(raw);
    for (const auto& [key, value] : parsed.items())
        out[key] = value.is_string() ? value.get<std::string>() : value.dump();
    return out;
}

MultipartUpload uploadFromRow(const pqxx::row& row) {
    return {
        .upload_id = row["upload_id"].as<std::string>(),
        .vault_id = row["vault_id"].as<uint32_t>(),
        .object_key = row["object_key"].as<std::string>(),
        .initiated_by = row["initiated_by"].as<uint32_t>(),
        .initiated_at = ts(row, "initiated_at"),
        .content_type = row["content_type"].as<std::optional<std::string>>(),
        .metadata = metadataFromJson(row["metadata"].as<std::string>()),
        .storage_class = row["storage_class"].as<std::optional<std::string>>(),
        .aborted = row["aborted"].as<bool>(),
        .completed = row["completed"].as<bool>()
    };
}

MultipartPart partFromRow(const pqxx::row& row) {
    return {
        .upload_id = row["upload_id"].as<std::string>(),
        .part_number = row["part_number"].as<uint32_t>(),
        .etag = row["etag"].as<std::string>(),
        .size_bytes = row["size_bytes"].as<uint64_t>(),
        .md5 = from_hex_bytea(row["md5"].as<std::string>()),
        .path = row["path"].as<std::string>(),
        .created_at = ts(row, "created_at")
    };
}

std::string normalizeKey(std::string key) {
    while (!key.empty() && key.front() == '/') key.erase(key.begin());
    return key;
}

std::string makeMetadataJson(const std::map<std::string, std::string>& metadata) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, value] : metadata) out[key] = value;
    return out.dump();
}

std::string notHiddenByActiveTrashSql(const std::string& vaultIdExpr, const std::string& objectKeyExpr) {
    return
        "NOT EXISTS ("
        "  SELECT 1 "
        "  FROM files_trashed ft "
        "  WHERE ft.vault_id = " + vaultIdExpr + " "
        "    AND ft.path = ('/' || " + objectKeyExpr + ") "
        "    AND ft.deleted_at IS NULL "
        "    AND NOT EXISTS ("
        "      SELECT 1 "
        "      FROM fs_entry live "
        "      JOIN files live_file ON live_file.fs_entry_id = live.id "
        "      WHERE live.vault_id = ft.vault_id AND live.path = ft.path"
        "    )"
        ")";
}
}

uint32_t Gateway::createCredential(const GatewayCredential& credential) {
    return Transactions::exec("S3Gateway::createCredential", [&](pqxx::work& txn) {
        const auto principalUserId = credential.principal_user_id == 0
            ? credential.user_id
            : credential.principal_user_id;
        const auto compatibilityUserId = credential.user_id == 0
            ? principalUserId
            : credential.user_id;
        const auto res = txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_credentials
                    (user_id, created_by, principal_user_id, name, access_key, encrypted_secret_access_key, iv,
                     enabled, enforce_budget_for_local_requests, scope_mode, description, expires_at)
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10,
                        $11, CASE WHEN $12::bigint IS NULL THEN NULL ELSE TO_TIMESTAMP($12::double precision) END)
                ON CONFLICT (user_id, name) DO UPDATE SET
                    created_by = EXCLUDED.created_by,
                    principal_user_id = EXCLUDED.principal_user_id,
                    access_key = EXCLUDED.access_key,
                    encrypted_secret_access_key = EXCLUDED.encrypted_secret_access_key,
                    iv = EXCLUDED.iv,
                    enabled = EXCLUDED.enabled,
                    enforce_budget_for_local_requests = EXCLUDED.enforce_budget_for_local_requests,
                    scope_mode = EXCLUDED.scope_mode,
                    description = EXCLUDED.description,
                    expires_at = EXCLUDED.expires_at
                RETURNING id
            )SQL",
            pqxx::params{
                compatibilityUserId,
                credential.created_by,
                principalUserId,
                credential.name,
                credential.access_key,
                to_hex_bytea(credential.encrypted_secret_access_key),
                to_hex_bytea(credential.iv),
                credential.enabled,
                credential.enforce_budget_for_local_requests,
                credential.scope_mode.empty() ? std::string{"user_access"} : credential.scope_mode,
                credential.description,
                credential.expires_at
            });
        return res.one_field().as<uint32_t>();
    });
}

std::vector<GatewayCredential> Gateway::listCredentials(const std::optional<uint32_t> userId) {
    return Transactions::exec("S3Gateway::listCredentials", [&](pqxx::work& txn) {
        const auto res = userId
            ? txn.exec("SELECT * FROM s3_gateway_credentials WHERE principal_user_id = " + txn.quote(*userId) +
                       " OR (principal_user_id IS NULL AND user_id = " + txn.quote(*userId) + ") ORDER BY id")
            : txn.exec("SELECT * FROM s3_gateway_credentials ORDER BY id");
        std::vector<GatewayCredential> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(credentialFromRow(row));
        return out;
    });
}

std::vector<GatewayCredential> Gateway::listCredentialsForPrincipal(const uint32_t userId) {
    return listCredentials(userId);
}

std::vector<GatewayCredential> Gateway::listCredentialsAdmin(const bool includeDisabled) {
    return Transactions::exec("S3Gateway::listCredentialsAdmin", [&](pqxx::work& txn) {
        const auto res = txn.exec(std::string{"SELECT * FROM s3_gateway_credentials "} +
                                  (includeDisabled ? "" : "WHERE enabled = TRUE ") +
                                  "ORDER BY principal_user_id NULLS LAST, id");
        std::vector<GatewayCredential> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(credentialFromRow(row));
        return out;
    });
}

std::optional<GatewayCredential> Gateway::getCredentialByAccessKey(const std::string& accessKey) {
    return Transactions::exec("S3Gateway::getCredentialByAccessKey", [&](pqxx::work& txn) -> std::optional<GatewayCredential> {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_credentials WHERE access_key = $1",
            pqxx::params{accessKey});
        if (res.empty()) return std::nullopt;
        return credentialFromRow(res.one_row());
    });
}

bool Gateway::deleteCredentialByAccessKey(const std::string& accessKey) {
    return Transactions::exec("S3Gateway::deleteCredentialByAccessKey", [&](pqxx::work& txn) {
        return txn.exec("DELETE FROM s3_gateway_credentials WHERE access_key = $1", pqxx::params{accessKey}).affected_rows() > 0;
    });
}

bool Gateway::deleteCredentialByName(const uint32_t userId, const std::string& name) {
    return Transactions::exec("S3Gateway::deleteCredentialByName", [&](pqxx::work& txn) {
        return txn.exec(
            "DELETE FROM s3_gateway_credentials "
            "WHERE (principal_user_id = $1 OR (principal_user_id IS NULL AND user_id = $1)) AND name = $2",
            pqxx::params{userId, name}).affected_rows() > 0;
    });
}

void Gateway::updateCredentialLastUsed(const uint32_t id) {
    Transactions::exec("S3Gateway::updateCredentialLastUsed", [&](pqxx::work& txn) {
        txn.exec("UPDATE s3_gateway_credentials SET last_used_at = NOW() WHERE id = $1", pqxx::params{id});
    });
}

std::vector<CredentialVaultScope> Gateway::listCredentialScopes(const uint32_t credentialId) {
    return Transactions::exec("S3Gateway::listCredentialScopes", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_credential_vault_scope WHERE credential_id = $1 ORDER BY vault_id",
            pqxx::params{credentialId});
        std::vector<CredentialVaultScope> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(scopeFromRow(row));
        return out;
    });
}

void Gateway::replaceCredentialScopes(
    const uint32_t credentialId,
    const std::vector<CredentialVaultScope>& scopes) {
    Transactions::exec("S3Gateway::replaceCredentialScopes", [&](pqxx::work& txn) {
        txn.exec(
            "DELETE FROM s3_gateway_credential_vault_scope WHERE credential_id = $1",
            pqxx::params{credentialId});
        for (const auto& scope : scopes) {
            txn.exec(
                R"SQL(
                    INSERT INTO s3_gateway_credential_vault_scope
                        (credential_id, vault_id, can_list, can_read, can_write, can_delete, can_admin)
                    VALUES ($1, $2, $3, $4, $5, $6, $7)
                    ON CONFLICT (credential_id, vault_id) DO UPDATE SET
                        can_list = EXCLUDED.can_list,
                        can_read = EXCLUDED.can_read,
                        can_write = EXCLUDED.can_write,
                        can_delete = EXCLUDED.can_delete,
                        can_admin = EXCLUDED.can_admin
                )SQL",
                pqxx::params{
                    credentialId,
                    scope.vault_id,
                    scope.can_list,
                    scope.can_read,
                    scope.can_write,
                    scope.can_delete,
                    scope.can_admin
                });
        }
    });
}

std::optional<CredentialVaultScope> Gateway::getCredentialScopeForVault(
    const uint32_t credentialId,
    const uint32_t vaultId) {
    return Transactions::exec("S3Gateway::getCredentialScopeForVault", [&](pqxx::work& txn) -> std::optional<CredentialVaultScope> {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_credential_vault_scope WHERE credential_id = $1 AND vault_id = $2",
            pqxx::params{credentialId, vaultId});
        if (res.empty()) return std::nullopt;
        return scopeFromRow(res.one_row());
    });
}

void Gateway::updateCredentialScopeMode(
    const uint32_t credentialId,
    const std::string& scopeMode,
    const uint32_t principalUserId,
    const std::optional<uint32_t> createdBy,
    const std::optional<std::string> description,
    const std::optional<std::time_t> expiresAt,
    const std::optional<bool> enforceBudgetForLocalRequests) {
    Transactions::exec("S3Gateway::updateCredentialScopeMode", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                UPDATE s3_gateway_credentials
                SET scope_mode = $2,
                    principal_user_id = $3,
                    user_id = $3,
                    created_by = $4,
                    description = $5,
                    expires_at = CASE WHEN $6::bigint IS NULL THEN NULL ELSE TO_TIMESTAMP($6::double precision) END,
                    enforce_budget_for_local_requests = COALESCE($7::boolean, enforce_budget_for_local_requests)
                WHERE id = $1
            )SQL",
            pqxx::params{credentialId, scopeMode, principalUserId, createdBy, description, expiresAt, enforceBudgetForLocalRequests});
    });
}

void Gateway::recordSyncOrigin(
    const uint32_t vaultId,
    const std::string& objectKey,
    const std::string& operation,
    const std::optional<uint32_t> gatewayCredentialId,
    const std::string& requestUuid) {
    Transactions::exec("S3Gateway::recordSyncOrigin", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_sync_origin
                    (vault_id, object_key, operation, gateway_credential_id, request_uuid)
                VALUES ($1, $2, $3, $4, $5)
            )SQL",
            pqxx::params{vaultId, normalizeKey(objectKey), operation, gatewayCredentialId, requestUuid});
    });
}

void Gateway::bindBucket(const BucketBinding& binding) {
    Transactions::exec("S3Gateway::bindBucket", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_bucket (vault_id, bucket_name, api_exclusive, mode, created_by)
                VALUES ($1, $2, $3, $4, $5)
                ON CONFLICT (vault_id) DO UPDATE SET
                    bucket_name = EXCLUDED.bucket_name,
                    api_exclusive = EXCLUDED.api_exclusive,
                    mode = EXCLUDED.mode,
                    updated_at = NOW()
            )SQL",
            pqxx::params{
                binding.vault_id,
                binding.bucket_name,
                binding.api_exclusive,
                binding.mode,
                binding.created_by
            });
    });
}

bool Gateway::unbindBucket(const std::string& bucketName) {
    return Transactions::exec("S3Gateway::unbindBucket", [&](pqxx::work& txn) {
        return txn.exec(
            "DELETE FROM s3_gateway_bucket WHERE bucket_name = $1",
            pqxx::params{bucketName}).affected_rows() > 0;
    });
}

std::optional<BucketBinding> Gateway::resolveBucket(const std::string& bucketName) {
    return Transactions::exec("S3Gateway::resolveBucket", [&](pqxx::work& txn) -> std::optional<BucketBinding> {
        const auto res = txn.exec("SELECT * FROM s3_gateway_bucket WHERE bucket_name = $1", pqxx::params{bucketName});
        if (res.empty()) return std::nullopt;
        return bucketFromRow(res.one_row());
    });
}

std::vector<BucketBinding> Gateway::listBuckets(const std::optional<uint32_t> userId) {
    return Transactions::exec("S3Gateway::listBuckets", [&](pqxx::work& txn) {
        const auto sql = userId
            ? "SELECT b.* FROM s3_gateway_bucket b JOIN vault v ON v.id = b.vault_id WHERE v.owner_id = " + txn.quote(*userId) + " ORDER BY b.bucket_name"
            : "SELECT * FROM s3_gateway_bucket ORDER BY bucket_name";
        const auto res = txn.exec(sql);
        std::vector<BucketBinding> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(bucketFromRow(row));
        return out;
    });
}

void Gateway::upsertObject(const ObjectState& state) {
    Transactions::exec("S3Gateway::upsertObject", [&](pqxx::work& txn) {
        const auto lastModified = state.last_modified == 0 ? std::time(nullptr) : state.last_modified;
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_object
                    (vault_id, object_key, etag, size_bytes, content_type, storage_class, last_modified, multipart, part_count)
                VALUES ($1, $2, $3, $4, $5, $6, TO_TIMESTAMP($7::double precision), $8, $9)
                ON CONFLICT (vault_id, object_key) DO UPDATE SET
                    etag = EXCLUDED.etag,
                    size_bytes = EXCLUDED.size_bytes,
                    content_type = EXCLUDED.content_type,
                    storage_class = EXCLUDED.storage_class,
                    last_modified = EXCLUDED.last_modified,
                    multipart = EXCLUDED.multipart,
                    part_count = EXCLUDED.part_count
            )SQL",
            pqxx::params{
                state.vault_id,
                normalizeKey(state.object_key),
                state.etag,
                state.size_bytes,
                state.content_type,
                state.storage_class,
                static_cast<long long>(lastModified),
                state.multipart,
                state.part_count
            });
    });
}

std::optional<ObjectState> Gateway::getObjectState(const uint32_t vaultId, const std::string& objectKey) {
    return Transactions::exec("S3Gateway::getObjectState", [&](pqxx::work& txn) -> std::optional<ObjectState> {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_object "
            "WHERE vault_id = $1 AND object_key = $2 "
            "AND " + notHiddenByActiveTrashSql("s3_gateway_object.vault_id", "object_key"),
            pqxx::params{vaultId, normalizeKey(objectKey)});
        if (res.empty()) return std::nullopt;
        return objectFromRow(res.one_row());
    });
}

void Gateway::deleteObjectState(const uint32_t vaultId, const std::string& objectKey) {
    Transactions::exec("S3Gateway::deleteObjectState", [&](pqxx::work& txn) {
        txn.exec(
            "DELETE FROM s3_gateway_object WHERE vault_id = $1 AND object_key = $2",
            pqxx::params{vaultId, normalizeKey(objectKey)});
    });
}

void Gateway::deleteObjectStateAndRemoteIndex(const uint32_t vaultId, const std::string& objectKey) {
    Transactions::exec("S3Gateway::deleteObjectStateAndRemoteIndex", [&](pqxx::work& txn) {
        const auto key = normalizeKey(objectKey);
        txn.exec(
            "DELETE FROM s3_gateway_object_metadata WHERE vault_id = $1 AND object_key = $2",
            pqxx::params{vaultId, key});
        txn.exec(
            "DELETE FROM s3_gateway_object WHERE vault_id = $1 AND object_key = $2",
            pqxx::params{vaultId, key});
        txn.exec(
            "DELETE FROM remote_object_index WHERE vault_id = $1 AND object_key = $2",
            pqxx::params{vaultId, key});
    });
}

ObjectListResult Gateway::listObjectStates(const uint32_t vaultId, const ObjectListParams& params) {
    return Transactions::exec("S3Gateway::listObjectStates", [&](pqxx::work& txn) {
        const auto prefix = normalizeKey(params.prefix);
        auto after = params.continuation_token.value_or(params.start_after.value_or(""));
        const uint32_t maxKeys = std::min(params.max_keys, 1000u);

        if (maxKeys == 0) return ObjectListResult{};

        if (params.delimiter && !params.delimiter->empty()) {
            const auto res = txn.exec(
                R"SQL(
                    WITH source AS (
                        SELECT
                            *,
                            SUBSTRING(object_key FROM CHAR_LENGTH($2) + 1) AS rest
                        FROM s3_gateway_object
                        WHERE vault_id = $1
                          AND LEFT(object_key, CHAR_LENGTH($2)) = $2
                          AND )SQL" + notHiddenByActiveTrashSql("s3_gateway_object.vault_id", "object_key") + R"SQL(
                    ),
                    items AS (
                        SELECT DISTINCT
                            ($2 || SUBSTRING(rest FROM 1 FOR STRPOS(rest, $3) + CHAR_LENGTH($3) - 1)) AS item_key,
                            TRUE AS common_prefix,
                            vault_id,
                            NULL::TEXT AS object_key,
                            NULL::TEXT AS etag,
                            NULL::BIGINT AS size_bytes,
                            NULL::TEXT AS content_type,
                            NULL::TEXT AS storage_class,
                            NULL::TIMESTAMP AS last_modified,
                            NULL::BOOLEAN AS multipart,
                            NULL::INTEGER AS part_count
                        FROM source
                        WHERE STRPOS(rest, $3) > 0

                        UNION ALL

                        SELECT
                            object_key AS item_key,
                            FALSE AS common_prefix,
                            vault_id,
                            object_key,
                            etag,
                            size_bytes,
                            content_type,
                            storage_class,
                            last_modified,
                            multipart,
                            part_count
                        FROM source
                        WHERE STRPOS(rest, $3) = 0
                    )
                    SELECT *
                    FROM items
                    WHERE item_key > $4
                    ORDER BY item_key
                    LIMIT $5
                )SQL",
                pqxx::params{vaultId, prefix, *params.delimiter, after, maxKeys + 1});

            ObjectListResult out;
            uint32_t emitted = 0;
            std::optional<std::string> lastEmittedItemKey;
            for (const auto& row : res) {
                if (emitted >= maxKeys) {
                    out.is_truncated = true;
                    out.next_continuation_token = lastEmittedItemKey;
                    break;
                }

                if (row["common_prefix"].as<bool>()) {
                    out.common_prefixes.push_back(row["item_key"].as<std::string>());
                } else {
                    out.objects.push_back(objectFromRow(row));
                }
                lastEmittedItemKey = row["item_key"].as<std::string>();
                ++emitted;
            }
            return out;
        }

        const auto res = txn.exec(
                R"SQL(
                SELECT * FROM s3_gateway_object
                WHERE vault_id = $1
                  AND LEFT(object_key, CHAR_LENGTH($2)) = $2
                  AND object_key > $3
                  AND )SQL" + notHiddenByActiveTrashSql("s3_gateway_object.vault_id", "object_key") + R"SQL(
                ORDER BY object_key
                LIMIT $4
            )SQL",
            pqxx::params{vaultId, prefix, after, maxKeys + 1});

        ObjectListResult out;
        uint32_t emitted = 0;
        std::optional<std::string> lastEmittedObjectKey;

        for (const auto& row : res) {
            if (emitted >= maxKeys) {
                out.is_truncated = true;
                out.next_continuation_token = lastEmittedObjectKey;
                break;
            }

            auto object = objectFromRow(row);
            lastEmittedObjectKey = object.object_key;
            out.objects.push_back(std::move(object));
            ++emitted;
        }

        return out;
    });
}

void Gateway::upsertObjectMetadata(
    const uint32_t vaultId,
    const std::string& objectKey,
    const std::map<std::string, std::string>& metadata) {
    Transactions::exec("S3Gateway::upsertObjectMetadata", [&](pqxx::work& txn) {
        const auto key = normalizeKey(objectKey);
        txn.exec("DELETE FROM s3_gateway_object_metadata WHERE vault_id = $1 AND object_key = $2", pqxx::params{vaultId, key});
        for (const auto& [name, value] : metadata)
            txn.exec(
                R"SQL(
                    INSERT INTO s3_gateway_object_metadata (vault_id, object_key, name, value)
                    VALUES ($1, $2, $3, $4)
                    ON CONFLICT (vault_id, object_key, name) DO UPDATE SET value = EXCLUDED.value
                )SQL",
                pqxx::params{vaultId, key, name, value});
    });
}

std::map<std::string, std::string> Gateway::listObjectMetadata(const uint32_t vaultId, const std::string& objectKey) {
    return Transactions::exec("S3Gateway::listObjectMetadata", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            "SELECT name, value FROM s3_gateway_object_metadata WHERE vault_id = $1 AND object_key = $2 ORDER BY name",
            pqxx::params{vaultId, normalizeKey(objectKey)});
        std::map<std::string, std::string> out;
        for (const auto& row : res) out[row["name"].as<std::string>()] = row["value"].as<std::string>();
        return out;
    });
}

void Gateway::deleteObjectMetadata(const uint32_t vaultId, const std::string& objectKey) {
    Transactions::exec("S3Gateway::deleteObjectMetadata", [&](pqxx::work& txn) {
        txn.exec(
            "DELETE FROM s3_gateway_object_metadata WHERE vault_id = $1 AND object_key = $2",
            pqxx::params{vaultId, normalizeKey(objectKey)});
    });
}

void Gateway::createMultipartUpload(const MultipartUpload& upload) {
    Transactions::exec("S3Gateway::createMultipartUpload", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_multipart_upload
                    (upload_id, vault_id, object_key, initiated_by, content_type, metadata, storage_class)
                VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7)
            )SQL",
            pqxx::params{
                upload.upload_id,
                upload.vault_id,
                normalizeKey(upload.object_key),
                upload.initiated_by,
                upload.content_type,
                makeMetadataJson(upload.metadata),
                upload.storage_class
            });
    });
}

std::optional<MultipartUpload> Gateway::getMultipartUpload(const std::string& uploadId) {
    return Transactions::exec("S3Gateway::getMultipartUpload", [&](pqxx::work& txn) -> std::optional<MultipartUpload> {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_multipart_upload WHERE upload_id = $1 AND aborted = FALSE AND completed = FALSE",
            pqxx::params{uploadId});
        if (res.empty()) return std::nullopt;
        return uploadFromRow(res.one_row());
    });
}

std::vector<MultipartUpload> Gateway::listMultipartUploads(const uint32_t vaultId, const std::string& prefix) {
    return Transactions::exec("S3Gateway::listMultipartUploads", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            R"SQL(
                SELECT * FROM s3_gateway_multipart_upload
                WHERE vault_id = $1 AND LEFT(object_key, CHAR_LENGTH($2)) = $2 AND aborted = FALSE AND completed = FALSE
                ORDER BY object_key, initiated_at
            )SQL",
            pqxx::params{vaultId, normalizeKey(prefix)});
        std::vector<MultipartUpload> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(uploadFromRow(row));
        return out;
    });
}

std::vector<MultipartUpload> Gateway::listMultipartUploadsInitiatedBefore(const std::time_t cutoff) {
    return Transactions::exec("S3Gateway::listMultipartUploadsInitiatedBefore", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            R"SQL(
                SELECT * FROM s3_gateway_multipart_upload
                WHERE initiated_at < TO_TIMESTAMP($1::double precision)
                  AND aborted = FALSE
                  AND completed = FALSE
                ORDER BY initiated_at
            )SQL",
            pqxx::params{cutoff});
        std::vector<MultipartUpload> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(uploadFromRow(row));
        return out;
    });
}

void Gateway::markMultipartUploadCompleted(const std::string& uploadId) {
    Transactions::exec("S3Gateway::markMultipartUploadCompleted", [&](pqxx::work& txn) {
        txn.exec("UPDATE s3_gateway_multipart_upload SET completed = TRUE WHERE upload_id = $1", pqxx::params{uploadId});
    });
}

void Gateway::abortMultipartUpload(const std::string& uploadId) {
    Transactions::exec("S3Gateway::abortMultipartUpload", [&](pqxx::work& txn) {
        txn.exec("UPDATE s3_gateway_multipart_upload SET aborted = TRUE WHERE upload_id = $1", pqxx::params{uploadId});
    });
}

void Gateway::upsertMultipartPart(const MultipartPart& part) {
    Transactions::exec("S3Gateway::upsertMultipartPart", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_multipart_part
                    (upload_id, part_number, etag, size_bytes, md5, path)
                VALUES ($1, $2, $3, $4, $5, $6)
                ON CONFLICT (upload_id, part_number) DO UPDATE SET
                    etag = EXCLUDED.etag,
                    size_bytes = EXCLUDED.size_bytes,
                    md5 = EXCLUDED.md5,
                    path = EXCLUDED.path,
                    created_at = NOW()
            )SQL",
            pqxx::params{
                part.upload_id,
                part.part_number,
                part.etag,
                part.size_bytes,
                to_hex_bytea(part.md5),
                part.path.string()
            });
    });
}

std::vector<MultipartPart> Gateway::listMultipartParts(const std::string& uploadId) {
    return Transactions::exec("S3Gateway::listMultipartParts", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_multipart_part WHERE upload_id = $1 ORDER BY part_number",
            pqxx::params{uploadId});
        std::vector<MultipartPart> out;
        out.reserve(res.size());
        for (const auto& row : res) out.push_back(partFromRow(row));
        return out;
    });
}

std::optional<MultipartPart> Gateway::getMultipartPart(const std::string& uploadId, const uint32_t partNumber) {
    return Transactions::exec("S3Gateway::getMultipartPart", [&](pqxx::work& txn) -> std::optional<MultipartPart> {
        const auto res = txn.exec(
            "SELECT * FROM s3_gateway_multipart_part WHERE upload_id = $1 AND part_number = $2",
            pqxx::params{uploadId, partNumber});
        if (res.empty()) return std::nullopt;
        return partFromRow(res.one_row());
    });
}

void Gateway::deleteMultipartParts(const std::string& uploadId) {
    Transactions::exec("S3Gateway::deleteMultipartParts", [&](pqxx::work& txn) {
        txn.exec("DELETE FROM s3_gateway_multipart_part WHERE upload_id = $1", pqxx::params{uploadId});
    });
}

void Gateway::backfillObjectStateFromFs(const uint32_t vaultId) {
    Transactions::exec("S3Gateway::backfillObjectStateFromFs", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_object (vault_id, object_key, etag, size_bytes, content_type, last_modified)
                SELECT
                    e.vault_id,
                    ltrim(e.path, '/') AS object_key,
                    ('"vh-meta-' || md5(e.vault_id::text || ':' || e.path || ':' || f.size_bytes::text || ':' || e.updated_at::text || ':' || COALESCE(f.content_hash, '')) || '"') AS etag,
                    f.size_bytes,
                    f.mime_type,
                    e.updated_at
                FROM fs_entry e
                JOIN files f ON f.fs_entry_id = e.id
                WHERE e.vault_id = $1
                  AND e.path <> '/'
                  AND NOT EXISTS (
                      SELECT 1 FROM s3_gateway_object o
                      WHERE o.vault_id = e.vault_id AND o.object_key = ltrim(e.path, '/')
                  )
            )SQL",
            pqxx::params{vaultId});
    });
}

void Gateway::backfillObjectStateFromRemoteIndex(const uint32_t vaultId) {
    Transactions::exec("S3Gateway::backfillObjectStateFromRemoteIndex", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO s3_gateway_object (vault_id, object_key, etag, size_bytes, storage_class, last_modified)
                SELECT vault_id, object_key, COALESCE(etag, '""'), size_bytes, storage_class, last_modified
                FROM remote_object_index
                WHERE vault_id = $1
                  AND )SQL" + notHiddenByActiveTrashSql("remote_object_index.vault_id", "object_key") + R"SQL(
                ON CONFLICT (vault_id, object_key) DO NOTHING
            )SQL",
            pqxx::params{vaultId});
    });
}

} // namespace vh::db::query::s3
