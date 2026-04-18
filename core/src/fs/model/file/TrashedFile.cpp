#include "fs/model/file/Trashed.hpp"
#include <boost/uuid/string_generator.hpp>

#include "db/encoding/timestamp.hpp"
#include <pqxx/result>

using namespace vh::fs::model::file;
using namespace vh::db::encoding;

Trashed::Trashed(const pqxx::row& row)
    : id(row["id"].as<unsigned int>()),
      vault_id(row["vault_id"].as<unsigned int>()),
      base32_alias(row["base32_alias"].as<std::string>()),
      path(row["path"].as<std::string>()),
      backing_path(row["backing_path"].as<std::string>()),
      trashed_at(parsePostgresTimestamp(row["trashed_at"].as<std::string>())),
      trashed_by(row["trashed_by"].as<unsigned int>()),
      size_bytes(row["size_bytes"].as<uint64_t>()) {
    if (row["deleted_at"].is_null()) deleted_at = std::nullopt;
    else deleted_at = parsePostgresTimestamp(row["deleted_at"].as<std::string>());
}

std::vector<std::shared_ptr<Trashed>> vh::fs::model::file::trashed_files_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Trashed>> files;
    files.reserve(res.size());
    for (const auto& row : res) files.emplace_back(std::make_shared<Trashed>(row));
    return files;
}
