#include "rbac/role/Admin.hpp"
#include "rbac/permission/Admin.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "usages.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using namespace vh::db::encoding;
using namespace vh::protocols::shell;

namespace vh::rbac::role {

Admin::Admin(const pqxx::row& row, const pqxx::result& globalVaultRoles)
    : Base(row),
      user_id(row["user_id"].as<uint32_t>()),
      permissions(row, globalVaultRoles) {
    assignment_id = row["assignment_id"].as<uint32_t>();
    user_id = ;
    assigned_at = parsePostgresTimestamp(row["assigned_at"].as<std::string>());
}

Admin::Admin(const nlohmann::json& j)
    : Base(j) {
    assignment_id = j.at("assignment_id").get<unsigned int>();
    user_id = j.at("user_id").get<unsigned int>();
    assigned_at = parsePostgresTimestamp(j.at("assigned_at").get<std::string>());
}

std::shared_ptr<Admin> Admin::fromJson(const nlohmann::json& j) {
    return std::make_shared<Admin>(j);
}

std::string Admin::permissions_to_flags_string() const {
    std::ostringstream oss;
    for (unsigned int i = 0; i < ADMIN_SHELL_PERMS.size(); ++i) {
        const auto bit = 1 << i;
        if (oss.tellp() > 0) oss << ' ';
        if (permissions & bit) oss << "--allow-" << ADMIN_SHELL_PERMS[i];
        else oss << "--deny-" << ADMIN_SHELL_PERMS[i];
    }
    return oss.str();
}

void to_json(nlohmann::json& j, const Admin& r) {
    to_json(j, static_cast<const Base&>(r));
    j["assignment_id"] = r.assignment_id;
    j["user_id"] = r.user_id;
    j["assigned_at"] = timestampToString(r.assigned_at);
}

void from_json(const nlohmann::json& j, Admin& r) {
    from_json(j, static_cast<Base&>(r));
    r.assignment_id = j.at("assignment_id").get<unsigned int>();
    r.user_id = j.at("user_id").get<unsigned int>();
    r.assigned_at = parsePostgresTimestamp(j.at("assigned_at").get<std::string>());
}

std::vector<std::shared_ptr<Admin>> user_roles_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Admin>> roles;
    roles.reserve(res.size());
    for (const auto& row : res) roles.push_back(std::make_shared<Admin>(row));
    return roles;
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Admin>>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.push_back(*role);
}

std::string to_string(const std::shared_ptr<Admin>& role) {
    const std::string prefix(4, ' ');
    std::string out = snake_case_to_title(role->name) + " (ID: " + std::to_string(role->id) + ")\n";
    out += prefix + "- Role ID: " + std::to_string(role->id) + "\n";
    out += prefix + "- Description: " + role->description + "\n";
    out += prefix + "- Type: " + role->type + "\n";
    out += prefix + "- Created at: " + timestampToString(role->created_at) + "\n";
    out += prefix + "- Assigned at: " + timestampToString(role->assigned_at) + "\n";
    out += prefix + "- Permissions:\n" + admin_perms_to_string(role->permissions, 12);
    return out;
}

}
