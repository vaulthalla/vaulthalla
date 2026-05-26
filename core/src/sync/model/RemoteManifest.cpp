#include "sync/model/RemoteManifest.hpp"

#include "db/encoding/timestamp.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/File.hpp"
#include "storage/s3/curl/helpers.hpp"

#include <nlohmann/json.hpp>

#include <ctime>
#include <optional>
#include <stdexcept>

using vh::db::encoding::parsePostgresTimestamp;
using vh::db::encoding::timestampToString;
using vh::db::encoding::to_utf8_string;

namespace vh::sync::model::remote_manifest {

namespace {
    std::string object_key_for_manifest(const std::filesystem::path& path) {
        auto key = to_utf8_string(path.lexically_normal().u8string());
        while (!key.empty() && key.front() == '/') key.erase(key.begin());
        return key;
    }

    std::string checksum_objects(const nlohmann::json& objects) {
        return vh::storage::s3::curl::sha256Hex(objects.dump());
    }
}

bool isVaulthallaManifestKey(const std::filesystem::path& key) {
    const auto normalized = object_key_for_manifest(key);
    return normalized == INDEX_V1_KEY || normalized.starts_with(".vaulthalla/");
}

std::string buildIndexV1(const uint32_t vaultId, const std::vector<std::shared_ptr<fs::model::File>>& files) {
    nlohmann::json objects = nlohmann::json::array();

    for (const auto& file : files) {
        if (!file) continue;
        const auto objectKey = object_key_for_manifest(file->path);
        if (objectKey.empty() || isVaulthallaManifestKey(objectKey)) continue;

        const bool encrypted = file->remote_encrypted.value_or(
            file->encrypted_with_key_version > 0 || !file->encryption_iv.empty());

        nlohmann::json object = {
            {"key", objectKey},
            {"size_bytes", file->size_bytes},
            {"last_modified", file->updated_at == 0 ? nullptr : nlohmann::json(timestampToString(file->updated_at))},
            {"content_hash", file->content_hash ? nlohmann::json(*file->content_hash) : nlohmann::json(nullptr)},
            {"encrypted", encrypted},
            {"iv", file->encryption_iv.empty() ? nlohmann::json(nullptr) : nlohmann::json(file->encryption_iv)},
            {"key_version", file->encrypted_with_key_version == 0 ? nlohmann::json(nullptr) : nlohmann::json(file->encrypted_with_key_version)},
            {"etag", file->remote_etag ? nlohmann::json(*file->remote_etag) : nlohmann::json(nullptr)},
            {"version_id", file->remote_version_id ? nlohmann::json(*file->remote_version_id) : nlohmann::json(nullptr)},
            {"sequencer", file->remote_sequencer ? nlohmann::json(*file->remote_sequencer) : nlohmann::json(nullptr)},
            {"storage_class", file->remote_storage_class ? nlohmann::json(*file->remote_storage_class) : nlohmann::json(nullptr)},
            {"restore_status", file->remote_restore_status ? nlohmann::json(*file->remote_restore_status) : nlohmann::json(nullptr)}
        };
        objects.push_back(std::move(object));
    }

    const nlohmann::json manifest = {
        {"version", INDEX_V1_VERSION},
        {"vault_id", vaultId},
        {"producer", "vaulthalla"},
        {"client_id", "vaulthalla"},
        {"generation", std::time(nullptr)},
        {"generated_at", timestampToString(std::time(nullptr))},
        {"object_count", objects.size()},
        {"object_checksum", checksum_objects(objects)},
        {"objects", std::move(objects)}
    };

    return manifest.dump();
}

IndexV1Metadata inspectIndexV1Metadata(
    const std::string& manifest,
    const std::optional<uint32_t> expectedVaultId) {
    const auto parsed = nlohmann::json::parse(manifest);
    if (!parsed.is_object()) throw std::runtime_error("remote manifest must be a JSON object");
    if (parsed.value("version", 0u) != INDEX_V1_VERSION)
        throw std::runtime_error("unsupported remote manifest version");
    if (!parsed.contains("vault_id") || !parsed.at("vault_id").is_number_unsigned())
        throw std::runtime_error("remote manifest is missing vault_id");
    if (expectedVaultId && parsed.at("vault_id").get<uint32_t>() != *expectedVaultId)
        throw std::runtime_error("remote manifest vault_id does not match current vault");
    if (!parsed.contains("objects") || !parsed.at("objects").is_array())
        throw std::runtime_error("remote manifest is missing objects array");

    const auto& objects = parsed.at("objects");
    if (parsed.contains("object_count") && parsed.at("object_count").is_number_unsigned() &&
        parsed.at("object_count").get<uint64_t>() != objects.size())
        throw std::runtime_error("remote manifest object_count does not match objects array");
    if (parsed.contains("object_checksum") && parsed.at("object_checksum").is_string() &&
        parsed.at("object_checksum").get<std::string>() != checksum_objects(objects))
        throw std::runtime_error("remote manifest object_checksum does not match objects array");

    IndexV1Metadata metadata;
    metadata.version = parsed.at("version").get<uint32_t>();
    metadata.vault_id = parsed.at("vault_id").get<uint32_t>();
    metadata.object_count = objects.size();
    if (parsed.contains("generated_at") && parsed.at("generated_at").is_string())
        metadata.generated_at = parsePostgresTimestamp(parsed.at("generated_at").get<std::string>());
    if (parsed.contains("object_checksum") && parsed.at("object_checksum").is_string())
        metadata.object_checksum = parsed.at("object_checksum").get<std::string>();

    return metadata;
}

std::vector<std::shared_ptr<fs::model::File>> parseIndexV1(
    const std::string& manifest,
    const std::optional<uint32_t> expectedVaultId) {
    (void)inspectIndexV1Metadata(manifest, expectedVaultId);

    const auto parsed = nlohmann::json::parse(manifest);
    const auto& objects = parsed.at("objects");

    std::vector<std::shared_ptr<fs::model::File>> files;

    for (const auto& object : objects) {
        if (!object.is_object()) continue;
        const auto key = object.value("key", std::string{});
        if (key.empty() || isVaulthallaManifestKey(key)) continue;

        std::optional<std::time_t> updated;
        if (object.contains("last_modified") && object.at("last_modified").is_string())
            updated = parsePostgresTimestamp(object.at("last_modified").get<std::string>());

        auto file = std::make_shared<fs::model::File>(
            key,
            object.value("size_bytes", uint64_t{0}),
            updated);

        if (object.contains("etag") && object.at("etag").is_string())
            file->remote_etag = object.at("etag").get<std::string>();
        if (object.contains("version_id") && object.at("version_id").is_string())
            file->remote_version_id = object.at("version_id").get<std::string>();
        if (object.contains("sequencer") && object.at("sequencer").is_string())
            file->remote_sequencer = object.at("sequencer").get<std::string>();
        if (object.contains("content_hash") && object.at("content_hash").is_string())
            file->content_hash = object.at("content_hash").get<std::string>();
        if (object.contains("encrypted") && object.at("encrypted").is_boolean())
            file->remote_encrypted = object.at("encrypted").get<bool>();
        if (object.contains("iv") && object.at("iv").is_string())
            file->encryption_iv = object.at("iv").get<std::string>();
        if (object.contains("key_version") && object.at("key_version").is_number_unsigned())
            file->encrypted_with_key_version = object.at("key_version").get<unsigned int>();
        if (object.contains("storage_class") && object.at("storage_class").is_string())
            file->remote_storage_class = object.at("storage_class").get<std::string>();
        if (object.contains("restore_status") && object.at("restore_status").is_string())
            file->remote_restore_status = object.at("restore_status").get<std::string>();

        files.push_back(std::move(file));
    }

    return files;
}

}
