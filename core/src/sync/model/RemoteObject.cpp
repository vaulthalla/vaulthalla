#include "sync/model/RemoteObject.hpp"

#include "db/encoding/timestamp.hpp"
#include "fs/model/File.hpp"

#include <pqxx/row>
#include <utility>

using namespace vh::sync::model;
using namespace vh::db::encoding;

namespace {
    std::optional<std::string> optional_string(const pqxx::row& row, const char* name) {
        const auto field = row[name];
        if (field.is_null()) return std::nullopt;
        return field.as<std::string>();
    }

    std::filesystem::path normalize_object_key(std::filesystem::path key) {
        key = key.lexically_normal();
        auto native = key.string();
        while (!native.empty() && native.front() == '/') native.erase(native.begin());
        return native;
    }
}

RemoteObject::RemoteObject(const pqxx::row& row)
    : id(row["id"].as<uint32_t>()),
      vault_id(row["vault_id"].as<uint32_t>()),
      object_key(normalize_object_key(row["object_key"].as<std::string>())),
      size_bytes(row["size_bytes"].as<uint64_t>()),
      etag(optional_string(row, "etag")),
      storage_class(optional_string(row, "storage_class")),
      restore_status(optional_string(row, "restore_status")),
      source(row["source"].as<std::string>()) {
    if (!row["last_modified"].is_null())
        last_modified = parsePostgresTimestamp(row["last_modified"].as<std::string>());
}

RemoteObject::RemoteObject(const uint32_t vaultId, const std::shared_ptr<fs::model::File>& file, std::string src)
    : vault_id(vaultId),
      object_key(file ? normalize_object_key(file->path) : std::filesystem::path{}),
      size_bytes(file ? file->size_bytes : 0),
      last_modified(file ? file->updated_at : 0),
      etag(file ? file->remote_etag : std::nullopt),
      storage_class(file ? file->remote_storage_class : std::nullopt),
      restore_status(file ? file->remote_restore_status : std::nullopt),
      source(std::move(src)) {}

std::shared_ptr<vh::fs::model::File> RemoteObject::toFile() const {
    auto file = std::make_shared<vh::fs::model::File>(
        object_key.string(),
        size_bytes,
        last_modified == 0 ? std::optional<std::time_t>{} : std::make_optional(last_modified));
    file->remote_etag = etag;
    file->remote_storage_class = storage_class;
    file->remote_restore_status = restore_status;
    return file;
}
