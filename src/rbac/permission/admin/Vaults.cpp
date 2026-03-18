#include "rbac/permission/admin/Vaults.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin {

std::string Vaults::toFlagsString() const {
    std::ostringstream oss;
    oss << self.toFlagsString()
        << user.toFlagsString()
        << admin.toFlagsString();
    return oss.str();
}

std::string Vault::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Vault Permissions:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "View: " << bool_to_string(canView()) << in << "\n";
    oss << in << "View Stats: " << bool_to_string(canViewStats()) << in << "\n";
    oss << in << "Create : " << bool_to_string(canCreate()) << in << "\n";
    oss << in << "Edit : " << bool_to_string(canEdit()) << in << "\n";
    oss << in << "Remove : " << bool_to_string(canRemove()) << in << "\n";
    return oss.str();
}

std::string Vaults::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Global Vault Policies:\n";
    const auto in = std::string(indent + 2, ' ');
    const auto i = indent + 4;
    oss << in << "Self Policy:\n" << self.toString(i);
    oss << in << "User Policy:\n" << user.toString(i);
    oss << in << "Admin Policy:\n" << admin.toString(i);
    return oss.str();
}

std::string to_string(const Vaults::Type& type) {
    switch (type) {
        case Vaults::Type::Self: return "self";
        case Vaults::Type::User: return "user";
        case Vaults::Type::Admin: return "admin";
        default: throw std::runtime_error("Invalid vault type");
    }
}

Vaults::Type vault_type_from_string(const std::string& str) {
    if (str == "self") return Vaults::Type::Self;
    if (str == "user") return Vaults::Type::User;
    if (str == "admin") return Vaults::Type::Admin;
    throw std::runtime_error("Invalid vault type: " + str);
}

void to_json(nlohmann::json &j, const Vault &v) {
    j = {
        {"view", v.canView()},
        {"view_stats", v.canViewStats()},
        {"create", v.canCreate()},
        {"edit", v.canEdit()},
        {"remove", v.canRemove()}
    };
}

void from_json(const nlohmann::json &j, Vault &v) {
    v.clear();
    if (j.at("view").get<bool>()) v.grant(VaultPermissions::View);
    if (j.at("view_stats").get<bool>()) v.grant(VaultPermissions::ViewStats);
    if (j.at("create").get<bool>()) v.grant(VaultPermissions::Create);
    if (j.at("edit").get<bool>()) v.grant(VaultPermissions::Edit);
    if (j.at("remove").get<bool>()) v.grant(VaultPermissions::Remove);
}

void to_json(nlohmann::json& j, const Vaults& v) {
    j = {
        {"self", v.self},
        {"user", v.user},
        {"admin", v.admin}
    };
}

void from_json(const nlohmann::json& j, Vaults& v) {
    j.at("self").get_to(v.self);
    j.at("user").get_to(v.user);
    j.at("admin").get_to(v.admin);
}

}
