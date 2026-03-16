#include "db/query/identities/User.hpp"

#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/role/vault/Global.hpp"
#include "rbac/role/Admin.hpp"
#include "auth/model/RefreshToken.hpp"
#include "crypto/util/hash.hpp"
#include "db/Transactions.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"

#include <pqxx/pqxx>
#include <stdexcept>
#include <array>

using namespace vh::db::query::identities;
using namespace vh::auth::model;

using U = vh::identities::User;
using UserPtr = std::shared_ptr<U>;

namespace {

UserPtr hydrateUser(pqxx::work& txn, const pqxx::row& userRow) {
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

    // Existing/bridging shape for vault-role assignments and overrides.
    // Keep these wired however your current User model expects them.
    const auto rolesRes = txn.exec(
        pqxx::prepped{"get_user_and_group_assigned_vault_roles"},
        pqxx::params{userId}
    );

    const auto overridesRes = txn.exec(
        pqxx::prepped{"vault_permission_override_list_for_user_and_groups"},
        pqxx::params{userId}
    );

    return std::make_shared<U>(
        userRow,
        adminRoleRow,
        globalPoliciesRes,
        rolesRes,
        overridesRes
    );
}

} // namespace

UserPtr User::getUserByName(const std::string& name) {
    return Transactions::exec("User::getUserByName", [&](pqxx::work& txn) -> UserPtr {
        const auto res = txn.exec(
            pqxx::prepped{"get_user_by_name"},
            pqxx::params{name}
        );

        if (res.empty()) {
            log::Registry::db()->trace("[User] No user found with name: {}", name);
            return nullptr;
        }

        return hydrateUser(txn, res.one_row());
    });
}

UserPtr User::getUserById(const unsigned int id) {
    return Transactions::exec("User::getUserById", [&](pqxx::work& txn) -> UserPtr {
        const auto res = txn.exec(
            pqxx::prepped{"get_user"},
            pqxx::params{id}
        );

        if (res.empty()) {
            log::Registry::db()->trace("[User] No user found with id: {}", id);
            return nullptr;
        }

        return hydrateUser(txn, res.one_row());
    });
}

UserPtr User::getUserByLinuxUID(unsigned int linuxUid) {
    return Transactions::exec("User::getUserByLinuxUID", [&](pqxx::work& txn) -> UserPtr {
        const auto res = txn.exec(
            pqxx::prepped{"get_user_by_linux_uid"},
            pqxx::params{linuxUid}
        );

        if (res.empty()) {
            log::Registry::db()->debug("[User] No user found with Linux UID: {}", linuxUid);
            return nullptr;
        }

        return hydrateUser(txn, res.one_row());
    });
}

unsigned int User::getUserIdByLinuxUID(const unsigned int linuxUid) {
    return Transactions::exec("User::getUserIdByLinuxUID", [&](pqxx::work& txn) -> unsigned int {
        const auto res = txn.exec(
            pqxx::prepped{"get_user_id_by_linux_uid"},
            pqxx::params{linuxUid}
        );

        if (res.empty())
            throw std::runtime_error("No user found with Linux UID: " + std::to_string(linuxUid));

        return res.one_field().as<unsigned int>();
    });
}

unsigned int User::createUser(const UserPtr& user) {
    if (!user) throw std::invalid_argument("User::createUser received null user");

    log::Registry::db()->debug("[User] Creating user: {}", user->name);

    if (!user->roles.admin) throw std::runtime_error("User admin role must be set before creating a user");

    return Transactions::exec("User::createUser", [&](pqxx::work& txn) -> unsigned int {
        const pqxx::params userParams{
            user->name,
            user->email,
            user->password_hash,
            user->meta.is_active,
            user->meta.linux_uid,
            user->meta.updated_by
        };

        const auto userId = txn.exec(
            pqxx::prepped{"insert_user"},
            userParams
        ).one_row()[0].as<unsigned int>();

        // Singular admin-role assignment
        txn.exec(
            pqxx::prepped{"admin_role_assignment_upsert"},
            pqxx::params{userId, user->roles.admin->id}
        );

        const std::array userGlobalVaultRoles {
            user->roles.admin->vGlobals.self,
            user->roles.admin->vGlobals.user,
            user->roles.admin->vGlobals.admin
        };

        // User global vault policies
        for (const auto& globalPolicy : userGlobalVaultRoles) {
            txn.exec(
                pqxx::prepped{"user_global_vault_policy_upsert"},
                pqxx::params{
                    userId,
                    globalPolicy.template_role_id,
                    globalPolicy.enforce_template,
                    globalPolicy.scope,
                    globalPolicy.fs.files.toBitString(),
                    globalPolicy.fs.directories.toBitString(),
                    globalPolicy.sync.toBitString(),
                    globalPolicy.roles.toBitString()
                }
            );
        }

        // Existing vault role assignment shape preserved for now
        for (const auto& [_, role] : user->roles.vaults) {
            if (!role->assignment) {
                log::Registry::db()->debug("[User] User role assignment not set");
                continue;
            }

            pqxx::params roleParams{
                role->assignment->vault_id,
                "user",
                userId,
                role->assignment
            };

            const auto res = txn.exec(
                pqxx::prepped{"vault_role_assignment_upsert"},
                roleParams
            );

            if (res.empty()) continue;
            role->assignment_id = res.one_row()["id"].as<unsigned int>();
        }

        // TODO: update this when moving to shared ptr overrides or otherwise
        for (const auto& override : user->roles.vaults[0]->fs.overrides) {
                if (!override) continue;

                override->assignment_id = user->roles.vaults[0]->assignment_id;

                txn.exec(
                    pqxx::prepped{"vault_permission_override_upsert"},
                    pqxx::params{
                        override->assignment_id,
                        override->permission.id,
                        override->glob_path(),
                        override->enabled,
                        to_string(override->effect)
                    }
                );
            }

        return userId;
    });
}

void User::updateUser(const UserPtr& user) {
    if (!user) throw std::invalid_argument("User::updateUser received null user");

    Transactions::exec("User::updateUser", [&](pqxx::work& txn) {
        const pqxx::params userParams{
            user->id,
            user->name,
            user->email,
            user->password_hash,
            user->meta.is_active,
            user->meta.linux_uid,
            user->meta.updated_by
        };

        txn.exec(pqxx::prepped{"update_user"}, userParams);

        // Singular admin-role assignment
        if (user->roles.admin)
            txn.exec(
                pqxx::prepped{"admin_role_assignment_upsert"},
                pqxx::params{user->id, user->roles.admin->id}
            );

        // Simplest bridge shape for now: replace all global policies for user, then upsert current set.
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_delete_all_for_user"},
            pqxx::params{user->id}
        );

        const std::array userGlobalVaultRoles {
            user->roles.admin->vGlobals.self,
            user->roles.admin->vGlobals.user,
            user->roles.admin->vGlobals.admin
        };

        for (const auto& globalPolicy : userGlobalVaultRoles) {
            txn.exec(
                pqxx::prepped{"user_global_vault_policy_upsert"},
                pqxx::params{
                    user->id,
                    globalPolicy.template_role_id,
                    globalPolicy.enforce_template,
                    globalPolicy.scope,
                    globalPolicy.fs.files.toBitString(),
                    globalPolicy.fs.directories.toBitString(),
                    globalPolicy.sync.toBitString(),
                    globalPolicy.roles.toBitString(),
                }
            );
        }
    });
}

bool User::authenticateUser(const std::string& name, const std::string& password) {
    return Transactions::exec("User::authenticateUser", [&](pqxx::work& txn) -> bool {
        const auto res = txn.exec(
            pqxx::prepped{"get_user_by_name"},
            pqxx::params{name}
        );

        if (res.empty()) return false;

        const auto storedHash = res.one_row()["password_hash"].as<std::string>();
        return vh::crypto::hash::verifyPassword(password, storedHash);
    });
}

void User::updateUserPassword(const unsigned int userId, const std::string& newPassword) {
    Transactions::exec("User::updateUserPassword", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"update_user_password"},
            pqxx::params{userId, newPassword}
        );
    });
}

void User::deleteUser(const unsigned int userId) {
    Transactions::exec("User::deleteUser", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"delete_user"},
            pqxx::params{userId}
        );
    });
}

std::vector<UserPtr> User::listUsers(model::ListQueryParams&& params) {
    return Transactions::exec("User::listUsers", [&](pqxx::work& txn) -> std::vector<UserPtr> {
        const auto sql = model::appendPaginationAndFilter(
            "SELECT * FROM users",
            params,
            "id",
            "name"
        );

        const auto res = txn.exec(sql);
        std::vector<UserPtr> users;
        users.reserve(res.size());

        for (const auto& row : res)
            users.emplace_back(hydrateUser(txn, row));

        return users;
    });
}

void User::updateLastLoggedInUser(const unsigned int userId) {
    Transactions::exec("User::updateLastLoggedInUser", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"update_user_last_login"},
            pqxx::params{userId}
        );
    });
}

bool User::userExists(const std::string& name) {
    return Transactions::exec("User::userExists", [&](pqxx::work& txn) -> bool {
        return txn.exec(
            pqxx::prepped{"user_exists"},
            pqxx::params{name}
        ).one_field().as<bool>();
    });
}

bool User::adminUserExists() {
    return Transactions::exec("User::adminUserExists", [](pqxx::work& txn) -> bool {
        return txn.exec(
            pqxx::prepped{"admin_user_exists"}
        ).one_field().as<bool>();
    });
}

bool User::adminPasswordIsDefault() {
    return Transactions::exec("User::adminPasswordIsDefault", [](pqxx::work& txn) -> bool {
        const auto passwordHash = txn.exec(
            pqxx::prepped{"get_admin_password"}
        ).one_field().as<std::string>();

        return vh::crypto::hash::verifyPassword("vh!adm1n", passwordHash);
    });
}
