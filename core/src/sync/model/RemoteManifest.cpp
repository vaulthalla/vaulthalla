#include "sync/model/RemoteManifest.hpp"

#include "db/encoding/timestamp.hpp"
#include "db/encoding/u8.hpp"
#include "fs/model/File.hpp"

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

        nlohmann::json object = {
            {"key", objectKey},
            {"size_bytes", file->size_bytes},
            {"last_modified", file->updated_at == 0 ? nullptr : nlohmann::json(timestampToString(file->updated_at))},
            {"etag", file->remote_etag ? nlohmann::json(*file->remote_etag) : nlohmann::json(nullptr)},
            {"storage_class", file->remote_storage_class ? nlohmann::json(*file->remote_storage_class) : nlohmann::json(nullptr)},
            {"restore_status", file->remote_restore_status ? nlohmann::json(*file->remote_restore_status) : nlohmann::json(nullptr)}
        };
        objects.push_back(std::move(object));
    }

    const nlohmann::json manifest = {
        {"version", INDEX_V1_VERSION},
        {"vault_id", vaultId},
        {"generated_at", timestampToString(std::time(nullptr))},
        {"objects", std::move(objects)}
    };

    return manifest.dump();
}

std::vector<std::shared_ptr<fs::model::File>> parseIndexV1(const std::string& manifest) {
    const auto parsed = nlohmann::json::parse(manifest);
    if (!parsed.is_object()) throw std::runtime_error("remote manifest must be a JSON object");
    if (parsed.value("version", 0u) != INDEX_V1_VERSION)
        throw std::runtime_error("unsupported remote manifest version");
    if (!parsed.contains("objects") || !parsed.at("objects").is_array())
        throw std::runtime_error("remote manifest is missing objects array");

    std::vector<std::shared_ptr<fs::model::File>> files;

    for (const auto& object : parsed.at("objects")) {
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
        if (object.contains("storage_class") && object.at("storage_class").is_string())
            file->remote_storage_class = object.at("storage_class").get<std::string>();
        if (object.contains("restore_status") && object.at("restore_status").is_string())
            file->remote_restore_status = object.at("restore_status").get<std::string>();

        files.push_back(std::move(file));
    }

    return files;
}

}
