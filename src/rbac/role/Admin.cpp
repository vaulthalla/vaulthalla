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
    : Meta(row),
      user_id(row["user_id"].as<uint32_t>()),
      permissions(row, globalVaultRoles) {}

Admin::Admin(const pqxx::row& row) : Admin(row, {}) {}

Admin::Admin(const nlohmann::json& j)
    : Meta(j) {
    if (j.contains("user_id")) user_id = j["user_id"].get<uint32_t>();
    if (j.contains("permissions")) permissions = j["permissions"].get<permission::Admin>();
}

Admin Admin::fromJson(const nlohmann::json& j) { return Admin(j); }

std::vector<Admin> admin_roles_from_pq_res(const pqxx::result& res) {
    std::vector<Admin> roles;
    roles.reserve(res.size());
    for (const auto& row : res) roles.emplace_back(row);
    return roles;
}

void to_json(nlohmann::json& j, const Admin& a) {
    j = static_cast<const Meta&>(a);
    j["user_id"] = a.user_id;
    j["permissions"] = a.permissions;
}

void from_json(const nlohmann::json& j, Admin& a) { a = Admin(j); }

void to_json(nlohmann::json& j, const std::vector<Admin>& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.emplace_back(role);
}

std::string Admin::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << snake_case_to_title(name) << " (ID: " << id << ")\n";
    const std::string in(indent + 2, ' ');
    if (user_id) oss << in << "- User ID: " << std::to_string(*user_id) << std::endl;
    oss << Meta::toString(indent);
    oss << permissions.toString(indent);
    return oss.str();
}

std::string to_string(const Admin& r) { return r.toString(); }

}
