#include "rbac/role/Meta.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>
#include <ostream>

using namespace vh::db::encoding;

namespace vh::rbac::role {

BasicMeta::BasicMeta(const pqxx::row& row)
    : created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())),
      assigned_at(parsePostgresTimestamp(row["assigned_at"].as<std::string>())) {}

Meta::Meta(const pqxx::row& row)
    : BasicMeta(row),
      assignment_id(row["assignment_id"].as<uint32_t>()),
      name(row["name"].as<std::string>()),
      description(row["description"].as<std::string>()) {
    if (!row["role_id"].is_null()) id = parsePostgresTimestamp(row["role_id"].as<std::string>());
    else if (!row["id"].is_null()) id = parsePostgresTimestamp(row["id"].as<std::string>());
}

Meta::Meta(const nlohmann::json& json) { from_json(json, *this); }

std::string BasicMeta::toString(const uint8_t indent) const {
    const std::string in(indent + 2, ' ');
    std::ostringstream oss;
    oss << in << "- Created At: " << timestampToString(created_at) << std::endl;
    oss << in << "- Updated At: " << timestampToString(updated_at) << std::endl;
    if (assigned_at) oss << in << "- Assigned At: " << timestampToString(*assigned_at) << std::endl;
    return oss.str();
}

std::string Meta::toString(const uint8_t indent) const {
    const std::string in(indent + 2, ' ');
    std::ostringstream oss;
    oss << in << "- Id: " << std::to_string(id) << std::endl;
    oss << in << "- Assignment ID: " << std::to_string(assignment_id) << std::endl;
    oss << in << "- Name: " << name << std::endl;
    oss << in << "- Description: " << description << std::endl;
    oss << BasicMeta::toString(indent);
    return oss.str();
}

void to_json(nlohmann::json& j, const BasicMeta& m) {
    j = {
        {"created_at", m.created_at},
        {"updated_at", m.updated_at},
        {"assigned_at", m.assigned_at},
    };
}

void to_json(nlohmann::json& j, const Meta& m) {
    j = static_cast<BasicMeta>(m);
    j["id"] = m.id;
    j["assigned_at"] = m.assigned_at;
    j["name"] = m.name;
    j["description"] = m.description;
}

void from_json(const nlohmann::json& j, Meta& m) {
    // keep this tight, don't let client dictate timestamps or id's
    j.at("name").get_to(m.name);
    j.at("description").get_to(m.description);
}

}
