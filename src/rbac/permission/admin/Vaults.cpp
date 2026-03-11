#include "rbac/permission/admin/Vaults.hpp"

#include <pqxx/result>

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

}
