#include "rbac/model/UserRole.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "usages.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using namespace vh::rbac::model;
using namespace vh::db::encoding;
using namespace vh::protocols::shell;

UserRole::UserRole(const pqxx::row& row)
    : Role(row) {
    assignment_id = row["assignment_id"].as<unsigned int>();
    user_id = row["user_id"].as<unsigned int>();
    assigned_at = parsePostgresTimestamp(row["assigned_at"].as<std::string>());
}

UserRole::UserRole(const nlohmann::json& j)
    : Role(j) {
    assignment_id = j.at("assignment_id").get<unsigned int>();
    user_id = j.at("user_id").get<unsigned int>();
    assigned_at = parsePostgresTimestamp(j.at("assigned_at").get<std::string>());
}

std::shared_ptr<UserRole> UserRole::fromJson(const nlohmann::json& j) {
    return std::make_shared<UserRole>(j);
}

std::string UserRole::permissions_to_flags_string() const {
    std::ostringstream oss;
    for (unsigned int i = 0; i < ADMIN_SHELL_PERMS.size(); ++i) {
        const auto bit = 1 << i;
        if (oss.tellp() > 0) oss << ' ';
        if (permissions & bit) oss << "--allow-" << ADMIN_SHELL_PERMS[i];
        else oss << "--deny-" << ADMIN_SHELL_PERMS[i];
    }
    return oss.str();
}

void vh::rbac::model::to_json(nlohmann::json& j, const UserRole& r) {
    to_json(j, static_cast<const Role&>(r));
    j["assignment_id"] = r.assignment_id;
    j["user_id"] = r.user_id;
    j["assigned_at"] = timestampToString(r.assigned_at);
}

void vh::rbac::model::from_json(const nlohmann::json& j, UserRole& r) {
    from_json(j, static_cast<Role&>(r));
    r.assignment_id = j.at("assignment_id").get<unsigned int>();
    r.user_id = j.at("user_id").get<unsigned int>();
    r.assigned_at = parsePostgresTimestamp(j.at("assigned_at").get<std::string>());
}

std::vector<std::shared_ptr<UserRole>> vh::rbac::model::user_roles_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<UserRole>> roles;
    roles.reserve(res.size());
    for (const auto& row : res) roles.push_back(std::make_shared<UserRole>(row));
    return roles;
}

void vh::rbac::model::to_json(nlohmann::json& j, const std::vector<std::shared_ptr<UserRole>>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.push_back(*role);
}

std::string vh::rbac::model::to_string(const std::shared_ptr<UserRole>& role) {
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
