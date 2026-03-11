#pragma once

#include "Permission.hpp"
#include "rbac/glob/model/Pattern.hpp"

#include <regex>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
    class result;
}

namespace vh::rbac::permission {

enum class OverrideOpt { ALLOW, DENY };

std::string to_string(const OverrideOpt& opt);
OverrideOpt overrideOptFromString(const std::string& str);

struct Override {
    enum class PermissionModule { Filesystem, Sync, Role, Key };
    unsigned int id{0};
    Permission permission;
    OverrideOpt effect{OverrideOpt::ALLOW};
    unsigned int assignment_id{0}; // ID of the vault_role_assignment this override is assigned to
    bool enabled{true};
    glob::model::Pattern pattern;

    Override() = default;
    explicit Override(const pqxx::row& row);
    explicit Override(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Override& po);
void from_json(const nlohmann::json& j, Override& po);

std::vector<std::shared_ptr<Override>> permissionOverridesFromPqRes(const pqxx::result& res);
std::vector<std::shared_ptr<Override>> permissionOverridesFromJson(const nlohmann::json& j);
void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Override>>& overrides);

std::string to_string(const std::shared_ptr<Override>& override);
std::string to_string(const std::vector<std::shared_ptr<Override>>& overrides);

std::string to_string(const Override::PermissionModule& module);
Override::PermissionModule permission_module_from_string(const std::string& str);

}
