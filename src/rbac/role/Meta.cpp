#include "rbac/role/Meta.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/has.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>
#include <ostream>

using namespace vh::db::encoding;

namespace vh::rbac::role {
    BasicMeta::BasicMeta(const pqxx::row &row) {
        if (hasColumn(row, "role_created_at") && !row["role_created_at"].is_null())
            created_at = parsePostgresTimestamp(row["role_created_at"].as<std::string>());
        else if (hasColumn(row, "created_at") && !row["created_at"].is_null())
            created_at = parsePostgresTimestamp(row["created_at"].as<std::string>());

        if (hasColumn(row, "role_updated_at") && !row["role_updated_at"].is_null())
            updated_at = parsePostgresTimestamp(row["role_updated_at"].as<std::string>());
        else if (hasColumn(row, "updated_at") && !row["updated_at"].is_null())
            updated_at = parsePostgresTimestamp(row["updated_at"].as<std::string>());

        if (hasColumn(row, "role_assigned_at") && !row["role_assigned_at"].is_null())
            assigned_at = parsePostgresTimestamp(row["role_assigned_at"].as<std::string>());
        else if (hasColumn(row, "assigned_at") && !row["assigned_at"].is_null())
            assigned_at = parsePostgresTimestamp(row["assigned_at"].as<std::string>());
    }

    Meta::Meta(const pqxx::row &row)
        : BasicMeta(row) {
        if (hasColumn(row, "role_id") && !row["role_id"].is_null())
            id = row["role_id"].as<uint32_t>();
        else if (hasColumn(row, "id") && !row["id"].is_null())
            id = row["id"].as<uint32_t>();

        if (hasColumn(row, "role_assignment_id") && !row["role_assignment_id"].is_null())
            assignment_id = row["role_assignment_id"].as<uint32_t>();
        else if (hasColumn(row, "assignment_id") && !row["assignment_id"].is_null())
            assignment_id = row["assignment_id"].as<uint32_t>();

        if (hasColumn(row, "role_name") && !row["role_name"].is_null())
            name = row["role_name"].as<std::string>();
        else if (hasColumn(row, "name") && !row["name"].is_null())
            name = row["name"].as<std::string>();

        if (hasColumn(row, "role_description") && !row["role_description"].is_null())
            description = row["role_description"].as<std::string>();
        else if (hasColumn(row, "description") && !row["description"].is_null())
            description = row["description"].as<std::string>();
    }

    Meta::Meta(const nlohmann::json &json) { from_json(json, *this); }

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

    void to_json(nlohmann::json &j, const BasicMeta &m) {
        j = {
            {"created_at", m.created_at},
            {"updated_at", m.updated_at},
            {"assigned_at", m.assigned_at},
        };
    }

    void to_json(nlohmann::json &j, const Meta &m) {
        j = static_cast<BasicMeta>(m);
        j["id"] = m.id;
        j["assigned_at"] = m.assigned_at;
        j["name"] = m.name;
        j["description"] = m.description;
    }

    void from_json(const nlohmann::json &j, Meta &m) {
        // keep this tight, don't let client dictate timestamps or id's
        j.at("name").get_to(m.name);
        j.at("description").get_to(m.description);
    }
}
