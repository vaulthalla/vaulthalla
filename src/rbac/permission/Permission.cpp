#include "rbac/permission/Permission.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>

using namespace vh::db::encoding;

namespace vh::rbac::permission {

Permission::Permission(const pqxx::row& row)
    : bit_position(row["bit_position"].as<uint32_t>()),
      name(row["name"].as<std::string>()),
      description(row["description"].as<std::string>()),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())) {
    if (!row["permission_override_id"].is_null()) id = row["permission_override_id"].as<uint32_t>();
    else if (!row["permission_id"].is_null()) id = row["permission_id"].as<uint32_t>();
    else if (!row["id"].is_null()) id = row["id"].as<uint32_t>();
}

Permission::Permission(const nlohmann::json& j)
    : id(j.at("id").get<uint32_t>()),
      bit_position(j.at("bit_position").get<uint32_t>()),
      name(j.at("name").get<std::string>()),
      description(j.at("description").get<std::string>()),
      created_at(parsePostgresTimestamp(j.at("created_at").get<std::string>())),
      updated_at(parsePostgresTimestamp(j.at("updated_at").get<std::string>())) {}

Permission::Permission(const uint32_t bitPos, std::string name, std::string description)
    : bit_position(bitPos), name(std::move(name)), description(std::move(description)) {}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Permission> >& permissions) {
    j = nlohmann::json::array();
    for (const auto& perm : permissions) j.emplace_back(*perm);
}

void from_json(const nlohmann::json& j, Permission& p) {
    p.id = j.at("id").get<uint32_t>();
    p.bit_position = j.at("bit_position").get<uint32_t>();
    p.name = j.at("name").get<std::string>();
    p.description = j.at("description").get<std::string>();
    p.created_at = parsePostgresTimestamp(j.at("created_at").get<std::string>());
    p.updated_at = parsePostgresTimestamp(j.at("updated_at").get<std::string>());
}

void to_json(nlohmann::json& j, const Permission& p) {
    j = {
        {"id", p.id},
        {"bit_position", p.bit_position},
        {"name", p.name},
        {"description", p.description},
        {"created_at", timestampToString(p.created_at)},
        {"updated_at", timestampToString(p.updated_at)},
    };
}

}
