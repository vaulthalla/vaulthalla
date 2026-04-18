#include "crypto/model/Secret.hpp"
#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/row>

using namespace vh::crypto::model;
using namespace vh::db::encoding;

Secret::Secret(const pqxx::row& row)
    : key(row["key"].c_str()),
      value(from_hex_bytea(row["value"].as<std::string>())),
      iv(from_hex_bytea(row["iv"].as<std::string>())),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())) {}
