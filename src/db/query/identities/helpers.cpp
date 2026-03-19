#include "db/query/identities/helpers.hpp"
#include "rbac/role/Vault.hpp"
#include "log/Registry.hpp"
#include "identities/User.hpp"
#include "identities/Group.hpp"
#include "rbac/permission/admin/VaultGlobals.hpp"
#include "rbac/role/Admin.hpp"

#include <array>

namespace vh::db::query::identities {

    std::shared_ptr<vh::identities::User> hydrateUser(pqxx::work &txn, const pqxx::row &userRow) {
        const auto userId = userRow["id"].as<unsigned int>();

        // Singular admin-role assignment joined to full admin_role
        const auto adminRoleRes = txn.exec(
            pqxx::prepped{"admin_role_assignment_get_by_user_id"},
            pqxx::params{userId}
        );

        if (adminRoleRes.empty())
            throw std::runtime_error("User " + std::to_string(userId) + " is missing an admin role assignment");

        const auto adminRoleRow = adminRoleRes.one_row();

        // Three global vault policy rows: self / user / admin
        const auto globalPoliciesRes = txn.exec(
            pqxx::prepped{"user_global_vault_policy_list_by_user"},
            pqxx::params{userId}
        );

        return std::make_shared<vh::identities::User>(
            userRow,
            adminRoleRow,
            globalPoliciesRes,
            getVaultRoles(txn, "user", userId),
            getUserGroups(txn, userId)
        );
    }

    void upsertUserRoles(pqxx::work &txn, const std::shared_ptr<vh::identities::User>& user) {
        upsertAdminRoleAssignment(txn, user->id, user->roles.admin->id);
        upsertGlobalVRoles(txn, user->roles.admin->vGlobals, user->id);
        upsertVaultRoles(txn, user->roles.vaults, "user", user->id);
    }

    std::shared_ptr<vh::identities::Group> hydrateGroup(pqxx::work &txn, const pqxx::row &groupRow) {
        const auto groupId = groupRow["id"].as<uint32_t>();
        const auto members = txn.exec(pqxx::prepped{"list_group_members"}, groupId);
        return std::make_shared<vh::identities::Group>(groupRow, members, getVaultRoles(txn, "group", groupId));
    }

    void upsertVaultRoles(pqxx::work& txn, const std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>>& vRoles,
                          const std::string &subjectType, uint32_t subjectId) {
        for (const auto &[_, role]: vRoles) {
            if (!role->assignment) {
                log::Registry::db()->debug("[User] User role assignment not set");
                continue;
            }

            pqxx::params roleParams{
                role->assignment->vault_id,
                subjectType,
                subjectId,
                role->id
            };

            const auto res = txn.exec(
                pqxx::prepped{"vault_role_assignment_upsert"},
                roleParams
            );

            if (res.empty()) continue;
            role->assignment_id = res.one_row()["id"].as<unsigned int>();

            for (auto &override: role->fs.overrides) {
                override.assignment_id = role->assignment_id;

                txn.exec(
                    pqxx::prepped{"vault_permission_override_upsert"},
                    pqxx::params{
                        override.assignment_id,
                        override.permission.id,
                        override.glob_path(),
                        override.enabled,
                        to_string(override.effect)
                    }
                );
            }
        }
    }

    void upsertGlobalVRoles(pqxx::work &txn, const rbac::permission::admin::VaultGlobals& vGlobal, const uint32_t userId) {
        const std::array userGlobalVaultRoles{ vGlobal.self, vGlobal.user, vGlobal.admin };

        for (const auto &globalPolicy: userGlobalVaultRoles) {
            txn.exec(
                pqxx::prepped{"user_global_vault_policy_upsert"},
                pqxx::params{
                    userId,
                    globalPolicy.template_role_id,
                    globalPolicy.enforce_template,
                    rbac::role::vault::to_string(globalPolicy.scope),
                    globalPolicy.fs.files.toBitString(),
                    globalPolicy.fs.directories.toBitString(),
                    globalPolicy.sync.toBitString(),
                    globalPolicy.roles.toBitString()
                }
            );
        }
    }

    void upsertAdminRoleAssignment(pqxx::work &txn, uint32_t userId, uint32_t roleId) {
        txn.exec(
                pqxx::prepped{"admin_role_assignment_upsert"},
                pqxx::params{userId, roleId}
            );
    }

    std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>> getVaultRoles(pqxx::work &txn, const std::string& subjectType, uint32_t subjectId) {
        std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>> vaultRoles;
        const auto res = txn.exec(
            pqxx::prepped{"vault_role_assignment_list_by_subject"},
            pqxx::params{subjectType, subjectId}
        );

        for (const auto &row: res) {
            const auto vaultId = row["vault_id"].as<uint32_t>();
            const auto assignmentId = row["assignment_id"].as<uint32_t>();

            const auto overridesRes = txn.exec(
                pqxx::prepped{"vault_permission_override_list_by_assignment_id"},
                pqxx::params{assignmentId}
            );

            vaultRoles[vaultId] = std::make_shared<rbac::role::Vault>(row, overridesRes);
        }

        return vaultRoles;
    }

    std::vector<std::shared_ptr<vh::identities::Group> > getUserGroups(pqxx::work &txn, uint32_t userId) {
        std::vector<std::shared_ptr<vh::identities::Group>> groups;

        const auto res = txn.exec(
            pqxx::prepped{"list_groups_for_user"},
            pqxx::params{userId}
        );

        for (const auto &row: res) groups.push_back(hydrateGroup(txn, row));

        return groups;
    }
}
