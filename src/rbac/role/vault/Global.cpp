#include "rbac/role/vault/Global.hpp"
#include "db/encoding/has.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <ostream>

using namespace vh::db::encoding;

namespace vh::rbac::role::vault {

Global::Global(const pqxx::row& row)
    : BasicMeta(row),
      Base(row),
      user_id(row["user_id"].as<uint32_t>()),
      scope(global_vault_role_scope_from_string(row["scope"].as<std::string>())),
      enforce_template(row["enforce_template"].as<bool>()) {
    if (hasColumn(row, "role_template_id") && !row["role_template_id"].is_null())
        template_role_id = row["role_template_id"].as<uint32_t>();
    else if (hasColumn(row, "template_id") && !row["template_id"].is_null())
        template_role_id = row["template_id"].as<uint32_t>();
}

Global::Global(const nlohmann::json& j)
    : BasicMeta(),  // nothing to instantiate from client
      Base(j),
      user_id(j.at("user_id").get<uint32_t>()),
      scope(global_vault_role_scope_from_string(j.at("scope").get<std::string>())),
      enforce_template(j.at("enforce_template").get<bool>()) {
        if (j.contains("template_id")) template_role_id = j.at("template_id").get<uint32_t>();
    }

Global Global::fromJson(const nlohmann::json& j) { return Global(j); }

std::string Global::toString(const uint8_t indent) const {
    std::ostringstream oss;
    const std::string in(indent + 2, ' ');
    oss << std::string(indent, ' ') << "Global Vault Role:\n";
    oss << in << "- User ID: " << std::to_string(user_id) << "\n";
    oss << in << "- Scope: " << to_string(scope) << "\n";
    oss << in << "- Template ID: " << (template_role_id ? std::to_string(*template_role_id) : "None") << "\n";
    oss << in << "- Enforce Template: " << bool_to_string(enforce_template) << "\n";
    oss << Base::toString(indent);
    return oss.str();
}

std::string to_string(const Global::Scope& scope) {
    switch (scope) {
        case Global::Scope::Self: return "self";
        case Global::Scope::User: return "user";
        case Global::Scope::Admin: return "admin";
        default: return "unknown";
    }
}

Global::Scope global_vault_role_scope_from_string(const std::string& name) {
    if (name == "self") return Global::Scope::Self;
    if (name == "user") return Global::Scope::User;
    if (name == "admin") return Global::Scope::Admin;
    throw std::invalid_argument("Invalid scope name: " + name);
}

void to_json(nlohmann::json& j, const Global& r) {
    j = static_cast<const BasicMeta&>(r);
    j["user_id"] = r.user_id;
    j["scope"] = r.scope;
    j["template_id"] = r.template_role_id ? std::to_string(*r.template_role_id) : "None";
    j["enforce_template"] = r.enforce_template ? "true" : "false";
    j["permissions"] = static_cast<const Base&>(r);
}

void from_json(const nlohmann::json& j, Global& r) { r = Global(j); }

std::vector<Global> global_vault_roles_from_json(const nlohmann::json& j) {
    std::vector<Global> roles;
    for (const auto& item : j) roles.emplace_back(item);
    return roles;
}

std::vector<Global> global_vault_roles_from_pq_result(const pqxx::result& res) {
    std::vector<Global> roles;
    for (const auto& row : res) roles.emplace_back(row);
    return roles;
}

}
