#include "protocols/RoleGuards.hpp"

#include "db/Transactions.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/admin/Assignments.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

#include <pqxx/pqxx>

namespace vh::protocols::roles {

namespace {

void requireCreatableName(const std::string& name, const std::string_view kind) {
    if (name.empty()) throw std::invalid_argument(std::string(kind) + " role name is required");
    if (name == kSuperAdminRoleName)
        throw RoleAlreadyExists("The built-in '" + std::string(kSuperAdminRoleName) + "' role already exists");
}

}

std::shared_ptr<rbac::role::Admin> createAdminRole(const std::shared_ptr<rbac::role::Admin>& role) {
    if (!role) throw std::invalid_argument("createAdminRole received null role");
    requireCreatableName(role->name, "Admin");

    const auto exists = [&] { return static_cast<bool>(db::query::rbac::role::Admin::get(role->name)); };
    if (exists()) throw RoleAlreadyExists("Admin role already exists: '" + role->name + "'");

    unsigned int id = 0;
    try {
        id = db::Transactions::exec("roles::createAdminRole", [&](pqxx::work& txn) {
            return txn.exec(
                pqxx::prepped{"admin_role_insert"},
                pqxx::params{
                    role->name,
                    role->description,
                    role->identities.toBitString(),
                    role->audits.toBitString(),
                    role->settings.toBitString(),
                    role->roles.toBitString(),
                    role->vaults.toBitString(),
                    role->keys.toBitString(),
                    role->s3Gateway.toBitString()
                }
            ).one_row()["id"].as<unsigned int>();
        });
    } catch (const pqxx::unique_violation&) {
        // Lost a race with a concurrent create of the same name; still never an overwrite.
        throw RoleAlreadyExists("Admin role already exists: '" + role->name + "'");
    }

    auto created = db::query::rbac::role::Admin::get(id);
    if (!created) throw std::runtime_error("Admin role '" + role->name + "' was created but could not be read back");
    return created;
}

std::shared_ptr<rbac::role::Vault> createVaultRole(const std::shared_ptr<rbac::role::Vault>& role) {
    if (!role) throw std::invalid_argument("createVaultRole received null role");
    if (role->name.empty()) throw std::invalid_argument("Vault role name is required");

    if (db::query::rbac::role::Vault::exists(role->name))
        throw RoleAlreadyExists("Vault role already exists: '" + role->name + "'");

    unsigned int id = 0;
    try {
        id = db::Transactions::exec("roles::createVaultRole", [&](pqxx::work& txn) {
            return txn.exec(
                pqxx::prepped{"vault_role_insert"},
                pqxx::params{
                    role->name,
                    role->description,
                    role->fs.files.toBitString(),
                    role->fs.directories.toBitString(),
                    role->sync.toBitString(),
                    role->roles.toBitString()
                }
            ).one_row()["id"].as<unsigned int>();
        });
    } catch (const pqxx::unique_violation&) {
        throw RoleAlreadyExists("Vault role already exists: '" + role->name + "'");
    }

    auto created = db::query::rbac::role::Vault::get(id);
    if (!created) throw std::runtime_error("Vault role '" + role->name + "' was created but could not be read back");
    return created;
}

std::optional<std::string> adminRoleUpdateError(const identities::User& caller,
                                                const rbac::role::Admin& existing,
                                                const rbac::role::Admin& staged) {
    if (existing.name == kSuperAdminRoleName)
        return "The built-in '" + std::string(kSuperAdminRoleName) + "' role cannot be modified";
    if (staged.name != existing.name && staged.name == kSuperAdminRoleName)
        return "Cannot rename a role to the reserved name '" + std::string(kSuperAdminRoleName) + "'";
    if (staged.name.empty()) return std::string("Admin role name cannot be empty");
    if (caller.roles.admin && caller.roles.admin->name == existing.name)
        return "Cannot modify the admin role assigned to your own account ('" + existing.name + "')";
    return std::nullopt;
}

std::optional<std::string> adminRoleDeleteError(const identities::User& caller, const rbac::role::Admin& existing) {
    if (existing.name == kSuperAdminRoleName)
        return "The built-in '" + std::string(kSuperAdminRoleName) + "' role cannot be deleted";
    if (caller.roles.admin && caller.roles.admin->name == existing.name)
        return "Cannot delete the admin role assigned to your own account ('" + existing.name + "')";
    if (db::query::rbac::role::admin::Assignments::countAssignmentsForRole(existing.id) > 0)
        return "Cannot delete role '" + existing.name +
               "' because it has active assignments. Remove those assignments before deleting the role.";
    return std::nullopt;
}

std::optional<std::string> vaultRoleDeleteError(const rbac::role::Vault& existing) {
    if (db::query::rbac::role::vault::Assignments::countAssignmentsForRole(existing.id) > 0)
        return "Cannot delete vault role '" + existing.name +
               "' because it has active assignments. Remove those assignments before deleting the role.";
    return std::nullopt;
}

}
