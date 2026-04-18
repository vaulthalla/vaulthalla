#include "fs/cache/Record.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

using namespace vh::fs::cache;
using namespace vh::db::encoding;

Record::Record(const pqxx::row& row)
    : id(row.at("id").as<unsigned int>()),
      vault_id(row.at("vault_id").as<unsigned int>()),
      file_id(row.at("file_id").as<unsigned int>()),
      path(row.at("path").as<std::string>()),
      type(typeFromString(row.at("type").as<std::string>())),
      size(row.at("size").as<uintmax_t>()),
      last_accessed(parsePostgresTimestamp(row.at("last_accessed").as<std::string>())),
      created_at(parsePostgresTimestamp(row.at("created_at").as<std::string>())) {}

void vh::fs::cache::to_json(nlohmann::json& j, const Record& index) {
    j = {
        {"id", index.id},
        {"vault_id", index.vault_id},
        {"file_id", index.file_id},
        {"path", index.path.string()},
        {"type", to_string(index.type)},
        {"size", index.size},
        {"last_accessed", timestampToString(index.last_accessed)},
        {"created_at", timestampToString(index.created_at)}
    };
}

void vh::fs::cache::from_json(const nlohmann::json& j, Record& index) {
    index.id = j.value("id", 0);
    index.vault_id = j.value("vault_id", 0);
    index.file_id = j.value("file_id", 0);
    index.path = j.value("path", std::filesystem::path{});
    index.type = typeFromString(j.value("type", "file"));
    index.size = j.value("size", 0u);
    index.last_accessed = parseTimestampFromString(j.value("last_accessed", ""));
    index.created_at = parseTimestampFromString(j.value("created_at", ""));
}

std::string vh::fs::cache::to_string(const Record::Type& type) {
    switch (type) {
        case Record::Type::File: return "file";
        case Record::Type::Thumbnail: return "thumbnail";
        default: throw std::invalid_argument("Unknown CacheIndex type");
    }
}

Record::Type vh::fs::cache::typeFromString(const std::string& str) {
    if (str == "file") return Record::Type::File;
    if (str == "thumbnail") return Record::Type::Thumbnail;
    throw std::invalid_argument("Unknown CacheIndex type: " + str);
}

std::vector<std::shared_ptr<Record>> vh::fs::cache::cache_indices_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Record>> indexes;
    indexes.reserve(res.size());
    for (const auto& row : res) indexes.emplace_back(std::make_shared<Record>(row));
    return indexes;
}
