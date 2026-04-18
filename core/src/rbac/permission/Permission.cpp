#include "rbac/permission/Permission.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/has.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>

using namespace vh::db::encoding;

namespace vh::rbac::permission {

Permission::Permission(const pqxx::row& row)
    : bit_position(row["bit_position"].as<uint32_t>()),
      qualified_name(row["name"].as<std::string>()),
      description(row["description"].as<std::string>()),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())) {
    if (const auto v = try_get<uint32_t>(row, std::vector<std::string_view>{"permission_id", "id"}))
        id = *v;

    if (const auto v = try_get<std::string>(row, std::vector<std::string_view>{"slug"})) slug = *v;
    else slug = qualified_name.substr(qualified_name.find_last_of('.') + 1);
}

Permission::Permission(const nlohmann::json& j)
    : id(j.at("id").get<uint32_t>()),
      bit_position(j.at("bit_position").get<uint32_t>()),
      qualified_name(j.at("name").get<std::string>()),
      description(j.at("description").get<std::string>()),
      created_at(parsePostgresTimestamp(j.at("created_at").get<std::string>())),
      updated_at(parsePostgresTimestamp(j.at("updated_at").get<std::string>())) {}

Permission::Permission(const uint32_t bitPos, std::string name, std::string description, const std::vector<std::string>& flags, uint64_t rawValue, std::type_index enumType)
    : bit_position(bitPos), qualified_name(std::move(name)), description(std::move(description)),
      flags(flags), rawValue(rawValue), enumType(enumType) {}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Permission> >& permissions) {
    j = nlohmann::json::array();
    for (const auto& perm : permissions) j.emplace_back(*perm);
}

void from_json(const nlohmann::json& j, Permission& p) {
    p.id = j.at("id").get<uint32_t>();
    p.bit_position = j.at("bit_position").get<uint32_t>();
    p.qualified_name = j.at("name").get<std::string>();
    p.description = j.at("description").get<std::string>();
    p.created_at = parsePostgresTimestamp(j.at("created_at").get<std::string>());
    p.updated_at = parsePostgresTimestamp(j.at("updated_at").get<std::string>());

    if (j.contains("slug")) p.slug = j.at("slug").get<std::string>();
}

void to_json(nlohmann::json& j, const Permission& p) {
    j = {
        {"bit_position", p.bit_position},
        {"qualified", p.qualified_name},
        {"description", p.description},
        {"slug", !p.slug.empty() ? p.slug : p.qualified_name.substr(p.qualified_name.find_last_of('.') + 1)}
    };

    if (p.id > 0) j["id"] = p.id;
    if (p.value.has_value()) j["value"] = *p.value;
}

}
