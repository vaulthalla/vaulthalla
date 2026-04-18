#include "rbac/permission/admin/VaultGlobals.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin {
    VaultGlobals::VaultGlobals(const pqxx::result &res) {
        for (const auto &row: res) {
            const auto scope = row["scope"].as<std::string>();
            if (scope == "self") self = role::vault::Global(row);
            else if (scope == "admin") admin = role::vault::Global(row);
            else if (scope == "user") user = role::vault::Global(row);
            else throw std::runtime_error("Invalid scope in VaultGlobals: " + scope);
        }
    }

    void to_json(nlohmann::json &j, const VaultGlobals &v) {
        j = {
                {"self", v.self},
                {"admin", v.admin},
                {"user",  v.user}
        };
    }

    void from_json(const nlohmann::json &j, VaultGlobals &v) {
        j.at("self").get_to(v.self);
        j.at("admin").get_to(v.admin);
        j.at("user").get_to(v.user);
    }
}
