#include "rbac/model/VaultRole.hpp"
#include "rbac/model/PermissionOverride.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "permsUtil.hpp"
#include "log/Registry.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace vh::db::encoding;
using namespace vh::protocols::shell;

namespace vh::rbac::model {

VaultRole::VaultRole(const pqxx::row& row, const pqxx::result& overrides)
    : Role(row),
      assignment_id(row["assignment_id"].as<unsigned int>()),
      subject_id(row["subject_id"].as<unsigned int>()),
      role_id(row["role_id"].as<unsigned int>()),
      vault_id(row["vault_id"].as<unsigned int>()),
      subject_type(row["subject_type"].as<std::string>()),
      assigned_at(parsePostgresTimestamp(row["assigned_at"].as<std::string>())),
      permission_overrides(permissionOverridesFromPqRes(overrides)) {
}

VaultRole::VaultRole(const pqxx::row& row, const std::vector<pqxx::row>& overrides)
    : Role(row),
      assignment_id(row["assignment_id"].as<unsigned int>()),
      subject_id(row["subject_id"].as<unsigned int>()),
      role_id(row["role_id"].as<unsigned int>()),
      vault_id(row["vault_id"].as<unsigned int>()),
      subject_type(row["subject_type"].as<std::string>()),
      assigned_at(parsePostgresTimestamp(row["assigned_at"].as<std::string>())) {
    for (const auto& override : overrides) permission_overrides.push_back(
        std::make_shared<PermissionOverride>(override));
}

VaultRole::VaultRole(const nlohmann::json& j)
    : Role(j),
      assignment_id(j.at("assignment_id").get<unsigned int>()),
      subject_id(j.at("subject_id").get<unsigned int>()),
      role_id(j.at("role_id").get<unsigned int>()),
      vault_id(j.at("vault_id").get<unsigned int>()),
      subject_type(j.at("subject_type").get<std::string>()),
      assigned_at(parsePostgresTimestamp(j.at("assigned_at").get<std::string>())),
      permission_overrides(j.at("permission_overrides")) {}

std::shared_ptr<VaultRole> VaultRole::fromJson(const nlohmann::json& j) {
    return std::make_shared<VaultRole>(j);
}

std::string VaultRole::permissions_to_flags_string() const {
    std::ostringstream oss;
    for (unsigned int i = 0; i < VAULT_SHELL_PERMS.size(); ++i) {
        const auto bit = 1 << i;
        if (oss.tellp() > 0) oss << ' ';
        if (permissions & bit) oss << "--allow-" << VAULT_SHELL_PERMS[i];
        else oss << "--deny-" << VAULT_SHELL_PERMS[i];
    }
    return oss.str();
}

void to_json(nlohmann::json& j, const VaultRole& r) {
    to_json(j, static_cast<const Role&>(r));
    j.update({
        {"assignment_id", r.assignment_id},
        {"vault_id", r.vault_id},
        {"subject_type", r.subject_type},
        {"subject_id", r.subject_id},
        {"assigned_at", timestampToString(r.assigned_at)},
        {"permission_overrides", r.permission_overrides}
    });
}

void from_json(const nlohmann::json& j, VaultRole& r) {
    from_json(j, static_cast<Role&>(r));
    r.assignment_id = j.at("assignment_id").get<unsigned int>();
    r.vault_id = j.at("vault_id").get<unsigned int>();
    r.subject_type = j.at("subject_type").get<std::string>();
    r.subject_id = j.at("subject_id").get<unsigned int>();
    r.role_id = j.at("role_id").get<unsigned int>();
    r.assigned_at = parsePostgresTimestamp(j.at("assigned_at").get<std::string>());
    r.permission_overrides = permissionOverridesFromJson(j.at("permission_overrides"));
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<VaultRole> >& roles) {
    j = nlohmann::json::array();
    for (const auto& role : roles) j.push_back(*role);
}

std::vector<std::shared_ptr<VaultRole> > vault_roles_vector_from_json(const nlohmann::json& j) {
    std::vector<std::shared_ptr<VaultRole> > roles;
    for (const auto& roleJson : j) roles.push_back(std::make_shared<VaultRole>(roleJson));
    return roles;
}

std::vector<std::shared_ptr<VaultRole> > vault_roles_vector_from_pq_result(
    const pqxx::result& res,
    const pqxx::result& overrides
    ) {
    std::vector<std::shared_ptr<VaultRole> > roles;

    std::unordered_map<unsigned int, std::vector<pqxx::row> > overrideMap;
    for (const auto& overrideRow : overrides) {
        const auto roleId = overrideRow["role_id"].as<unsigned int>();
        overrideMap[roleId].push_back(overrideRow);
    }

    for (const auto& item : res) {
        const auto roleId = item["role_id"].as<unsigned int>();
        const auto& roleOverrides = overrideMap[roleId];
        roles.push_back(std::make_shared<VaultRole>(item, roleOverrides));
    }

    return roles;
}

void to_json(nlohmann::json& j, const std::unordered_map<unsigned int, std::shared_ptr<VaultRole>>& roles) {
    j = nlohmann::json::array();
    for (const auto& [k, v] : roles) j.push_back(*v);
}

VRolePair vault_roles_from_json(const nlohmann::json& j) {
    VRolePair rolesPair;
    for (const auto& roleJson : j) {
        const auto role = std::make_shared<VaultRole>(roleJson);
        if (role->subject_type == "user") rolesPair.roles[role->vault_id] = role;
        else if (role->subject_type == "group") rolesPair.group_roles[role->vault_id] = role;
        else vh::log::Registry::auth()->warn("Unknown subject_type '{}' in vault role ID {}", role->subject_type, role->id);
    }
    return rolesPair;
}

VRolePair vault_roles_from_pq_result(
    const pqxx::result& res,
    const pqxx::result& overrides
    ) {

    std::unordered_map<unsigned int, std::vector<pqxx::row> > overrideMap;
    for (const auto& overrideRow : overrides) {
        const auto roleId = overrideRow["role_id"].as<unsigned int>();
        overrideMap[roleId].push_back(overrideRow);
    }

    VRolePair rolesPair;

    for (const auto& item : res) {
        const auto role = std::make_shared<VaultRole>(item, overrideMap[item["role_id"].as<unsigned int>()]);
        if (role->subject_type == "user") rolesPair.roles[role->vault_id] = role;
        else if (role->subject_type == "group") {
            if (rolesPair.group_roles.contains(role->vault_id)) {
                const auto existingRole = rolesPair.group_roles[role->vault_id];
                const auto combinedPerms = existingRole->permissions | role->permissions;
                rolesPair.group_roles[role->vault_id]->permissions = combinedPerms;
                vh::log::Registry::auth()->warn("Combining group role permissions for vault ID {}: existing perms {:04x} + new perms {:04x} = combined perms {:04x}",
                    role->vault_id, existingRole->permissions, role->permissions, combinedPerms);
            } else rolesPair.group_roles[role->vault_id] = role;
        }
        else vh::log::Registry::auth()->warn("Unknown subject_type '{}' in vault role ID {}", role->subject_type, role->id);
    }

    return rolesPair;
}

std::vector<std::shared_ptr<PermissionOverride> > VaultRole::getPermissionOverrides(const unsigned short bit) const {
    std::vector<std::shared_ptr<PermissionOverride>> overrides;
    for (const auto& override : permission_overrides) {
        if (override->permission.bit_position == bit) overrides.push_back(override);
        vh::log::Registry::auth()->debug("Checking override: {} for bit {}", to_string(override), bit);
    }
    return overrides;
}


bool VaultRole::canManageVault(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageVault, path);
}

bool VaultRole::canManageAccess(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageAccess, path);
}

bool VaultRole::canManageTags(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageTags, path);
}

bool VaultRole::canManageMetadata(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageMetadata, path);
}

bool VaultRole::canManageVersions(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageVersions, path);
}

bool VaultRole::canManageFileLocks(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::ManageFileLocks, path);
}

bool VaultRole::canShare(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Share, path);
}

bool VaultRole::canSync(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Sync, path);
}

bool VaultRole::canCreate(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Create, path);
}

bool VaultRole::canDownload(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Download, path);
}

bool VaultRole::canDelete(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Delete, path);
}

bool VaultRole::canRename(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Rename, path);
}

bool VaultRole::canMove(const std::filesystem::path& path) const {
    return validatePermission(permissions, VaultPermission::Move, path);
}

bool VaultRole::canList(const std::filesystem::path& path) const {
    if (path.empty()) return true; // If no path is specified, assume listing is allowed at the top level
    return validatePermission(permissions, VaultPermission::List, path);
}

std::string to_string(const std::shared_ptr<VaultRole>& role) {
    std::string out = snake_case_to_title(role->name) + " (ID: " + std::to_string(role->id) + ")\n";
    out += " - Role ID: " + std::to_string(role->id) + "\n";
    out += " - Description: " + role->description + "\n";
    out += " - Type: " + role->type + "\n";
    out += " - Subject Type: " + role->subject_type + "\n";
    out += " - Subject ID: " + std::to_string(role->subject_id) + "\n";
    out += " - Vault ID: " + std::to_string(role->vault_id) + "\n";
    out += " - Created at: " + timestampToString(role->created_at) + "\n";
    out += " - Assigned at: " + timestampToString(role->assigned_at) + "\n";
    out += " - Permissions:\n" + admin_perms_to_string(role->permissions, 12) + "\n";
    out += " - Permission Overrides: " + to_string(role->permission_overrides) + "\n";
    return out;
}

std::string to_string(const std::unordered_map<unsigned int, std::shared_ptr<VaultRole>>& roles) {
    if (roles.empty()) return "No vault roles found\n";

    std::string out;
    for (const auto& [_, v] : roles) out += to_string(v) + "\n";
    return out;
}

std::string to_string(const std::vector<std::shared_ptr<VaultRole>>& roles) {
    if (roles.empty()) return "No vault roles found\n";

    std::string out;
    for (const auto& role : roles) out += to_string(role) + "\n";
    return out;
}

}
