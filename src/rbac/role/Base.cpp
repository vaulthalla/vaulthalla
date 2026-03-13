#include "rbac/role/Base.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/util/lineHelpers.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <array>

using namespace vh::protocols::shell;
using namespace vh::db::encoding;

namespace vh::rbac::role {

Base::Base(const pqxx::row& row)
    : assignment_id(row["assignment_id"].as<uint32_t>()),
      name(row["name"].as<std::string>()),
      description(row["description"].as<std::string>()),
      type(row["type"].as<std::string>()),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())),
      assigned_at(parsePostgresTimestamp(row["assigned_at"].as<std::string>())) {
    std::array<std::string, 4> id_aliases { "admin_role_id", "vault_role_id", "role_id", "id" };
    for (const auto& id : id_aliases)
        if (!row[id].is_null()) {
            this->id = row[id].as<uint32_t>();
            break;
        }
}

Base::Base(const nlohmann::json& j)
    : id(j.contains("role_id") ? j.at("role_id").get<unsigned int>() : 0),
      name(j.at("name").get<std::string>()),
      description(j.at("description").get<std::string>()),
      type(j.at("type").get<std::string>()),
      created_at(static_cast<std::time_t>(0)),
      updated_at(static_cast<std::time_t>(0)),
      assigned_at(static_cast<std::time_t>(0)) {}

Base::Base(std::string name, std::string description, std::string type, const uint16_t permissions)
    : name(std::move(name)),
      description(std::move(description)),
      type(std::move(type)) {}

void to_json(nlohmann::json& j, const Base& r) {
    j = {
        {"role_id", r.id},
        {"name", r.name},
        {"description", r.description},
        {"type", r.type},
        {"permissions", r.type == "user" ? jsonFromAdminMask(r.permissions) : jsonFromVaultMask(r.permissions)},
        {"created_at", timestampToString(r.created_at)}
    };
}

void from_json(const nlohmann::json& j, Base& r) {
    if (j.contains("role_id")) r.id = j.at("role_id").get<unsigned int>();
    r.name = j.at("name").get<std::string>();
    r.description = j.at("description").get<std::string>();
    r.type = j.at("type").get<std::string>();
    r.permissions = r.type == "user" ? adminMaskFromJson(j.at("permissions")) : vaultMaskFromJson(j.at("permissions"));
    r.created_at = parsePostgresTimestamp(j.at("created_at").get<std::string>());
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Base>>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.push_back(*role);
}

std::vector<std::shared_ptr<Base>> roles_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Base> > roles;
    for (const auto& item : res) roles.push_back(std::make_shared<Base>(item));
    return roles;
}

std::string to_string(const std::shared_ptr<Base>& r) {
    std::string out = "Role:\n";
    out += "Role ID: " + std::to_string(r->id) + "\n";
    out += "Name: " + r->name + "\n";
    out += "Type: " + r->type + "\n";
    out += "Description: " + r->description + "\n";
    out += "Permissions: " + (r->type == "user" ? admin_perms_to_string(r->permissions) : vault_perms_to_string(r->permissions)) + "\n";
    out += "Created At: " + timestampToString(r->created_at) + "\n";
    return out;
}

std::string to_string(const std::vector<std::shared_ptr<Base>>& roles) {
    if (roles.empty()) return "No roles assigned";

    Table tbl({
        {"ID", Align::Left, 4, 8, false, false},
        {"Name", Align::Left, 4, 32, false, false},
        {"Type", Align::Left, 4, 16, false, false},
        {"Description", Align::Left, 4, 64, false, false},
        {"Created At", Align::Left, 4, 20, false, false}
    }, term_width());

    for (const auto& role : roles) {
        if (!role) continue; // Skip null pointers
        tbl.add_row({
            std::to_string(role->id),
            role->name,
            role->type,
            role->description,
            timestampToString(role->created_at)
        });
    }

    return "Roles:\n" + tbl.render();
}

std::string Base::underscore_to_hyphens(const std::string& s) {
    std::string result = s;
    std::ranges::replace(result.begin(), result.end(), '_', '-');
    return result;
}

std::string Base::permissions_to_flags_string() const {
    if (type == "user") return static_cast<Admin>(*this).permissions_to_flags_string();
    if (type == "vault") return static_cast<Vault>(*this).permissions_to_flags_string();
    throw std::runtime_error("Role: unknown role type for permissions_to_flags_string");
}

}
