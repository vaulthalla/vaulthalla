#include "rbac/permission/admin/Vaults.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin {

Vaults::Vaults(const pqxx::result& res) {
    for (const auto& row : res) {
        switch (vault_type_from_string(row["global_name"].as<std::string>())) {
            case Type::Self:
                self = Vault(row);
                break;
            case Type::User:
                user = Vault(row);
                break;
            case Type::Admin:
                admin = Vault(row);
                break;
            default:
                throw std::runtime_error("Invalid vault type in database: " + row["global_name"].as<std::string>());
        }
    }
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
