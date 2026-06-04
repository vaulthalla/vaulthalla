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
#include "db/query/identities/helpers.hpp"

#include <stdexcept>
#include <array>
#include <optional>
#include <pqxx/pqxx>

using U = vh::identities::User;
using UserPtr = std::shared_ptr<U>;

namespace vh::db::query::identities {
    namespace {
        bool rowBool(const pqxx::row& row, const char* column, const bool fallback = false) {
            try {
                const auto field = row[column];
                if (field.is_null()) return fallback;
                return field.as<bool>();
            } catch (...) {
                return fallback;
            }
        }

        void guardNormalUserCreate(const UserPtr& user) {
            if (user->isProtected || user->systemOnly)
                throw std::runtime_error("protected/system-only user flags are bootstrap-only");

            if (user->meta.linux_uid && *user->meta.linux_uid == 0)
                throw std::runtime_error("linux_uid 0 is reserved for the root principal");
        }

        void guardNormalUserUpdate(pqxx::work& txn, const UserPtr& user) {
            const auto res = txn.exec(
                R"SQL(
                    SELECT u.id, u.protected
                    FROM users u
                    WHERE u.id = $1
                )SQL",
                pqxx::params{user->id}
            );

            if (res.empty())
                throw std::runtime_error("User not found: " + std::to_string(user->id));

            if (rowBool(res.one_row(), "protected"))
                throw std::runtime_error("protected users cannot be updated through normal user paths");

            if (user->isProtected || user->systemOnly)
                throw std::runtime_error("protected/system-only user flags are bootstrap-only");

            if (user->meta.linux_uid && *user->meta.linux_uid == 0)
                throw std::runtime_error("linux_uid 0 is reserved for the root principal");
        }

        void guardNormalUserDelete(pqxx::work& txn, const unsigned int userId) {
            const auto res = txn.exec(
                "SELECT protected FROM users WHERE id = $1",
                pqxx::params{userId}
            );

            if (res.empty()) return;
            if (rowBool(res.one_row(), "protected"))
                throw std::runtime_error("protected users cannot be deleted");
        }

        bool systemOnlyUser(pqxx::work& txn, const unsigned int userId) {
            const auto res = txn.exec(
                "SELECT system_only FROM users WHERE id = $1",
                pqxx::params{userId}
            );
            return !res.empty() && rowBool(res.one_row(), "system_only");
        }
    }

    UserPtr User::getUserByName(const std::string &name) {
        return Transactions::exec("User::getUserByName", [&](pqxx::work &txn) -> UserPtr {
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
        return Transactions::exec("User::getUserById", [&](pqxx::work &txn) -> UserPtr {
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
        return Transactions::exec("User::getUserByLinuxUID", [&](pqxx::work &txn) -> UserPtr {
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
        return Transactions::exec("User::getUserIdByLinuxUID", [&](pqxx::work &txn) -> unsigned int {
            const auto res = txn.exec(
                pqxx::prepped{"get_user_id_by_linux_uid"},
                pqxx::params{linuxUid}
            );

            if (res.empty())
                throw std::runtime_error("No user found with Linux UID: " + std::to_string(linuxUid));

            return res.one_field().as<unsigned int>();
        });
    }

    unsigned int User::createUser(const UserPtr &user) {
        if (!user) throw std::invalid_argument("User::createUser received null user");
        guardNormalUserCreate(user);

        log::Registry::db()->debug("[User] Creating user: {}", user->name);

        if (!user->roles.admin) throw std::runtime_error("User admin role must be set before creating a user");

        return Transactions::exec("User::createUser", [&](pqxx::work &txn) -> unsigned int {
            const pqxx::params userParams{
                user->name,
                user->email,
                user->password_hash,
                user->meta.is_active,
                user->meta.linux_uid,
                user->meta.updated_by
            };

            user->id = txn.exec(
                pqxx::prepped{"insert_user"},
                userParams
            ).one_row()[0].as<uint32_t>();

            upsertUserRoles(txn, user);

            return user->id;
        });
    }

    void User::updateUser(const UserPtr &user) {
        if (!user) throw std::invalid_argument("User::updateUser received null user");

        Transactions::exec("User::updateUser", [&](pqxx::work &txn) {
            guardNormalUserUpdate(txn, user);

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

            // Simplest bridge shape for now: replace all global policies for user, then upsert current set.
            txn.exec(
                pqxx::prepped{"user_global_vault_policy_delete_all_for_user"},
                pqxx::params{user->id}
            );

            upsertUserRoles(txn, user);
        });
    }

    bool User::authenticateUser(const std::string &name, const std::string &password) {
        return Transactions::exec("User::authenticateUser", [&](pqxx::work &txn) -> bool {
            const auto res = txn.exec(
                pqxx::prepped{"get_user_by_name"},
                pqxx::params{name}
            );

            if (res.empty()) return false;
            if (rowBool(res.one_row(), "system_only")) return false;

            const auto storedHash = res.one_row()["password_hash"].as<std::string>();
            return vh::crypto::hash::verifyPassword(password, storedHash);
        });
    }

    void User::updateUserPassword(const unsigned int userId, const std::string &passwordHash) {
        Transactions::exec("User::updateUserPassword", [&](pqxx::work &txn) {
            if (systemOnlyUser(txn, userId))
                throw std::runtime_error("system-only users cannot receive normal user passwords");

            const auto res = txn.exec(
                pqxx::prepped{"update_user_password"},
                pqxx::params{userId, passwordHash}
            );

            if (res.affected_rows() != 1)
                throw std::runtime_error("User not found: " + std::to_string(userId));
        });
    }

    void User::deleteUser(const unsigned int userId) {
        Transactions::exec("User::deleteUser", [&](pqxx::work &txn) {
            guardNormalUserDelete(txn, userId);

            txn.exec(
                pqxx::prepped{"delete_user"},
                pqxx::params{userId}
            );
        });
    }

    std::vector<UserPtr> User::listUsers(model::ListQueryParams &&params) {
        return Transactions::exec("User::listUsers", [&](pqxx::work &txn) -> std::vector<UserPtr> {
            const auto sql = model::appendPaginationAndFilter(
                "SELECT * FROM users",
                params,
                "id",
                "name"
            );

            const auto res = txn.exec(sql);
            std::vector<UserPtr> users;
            users.reserve(res.size());

            for (const auto &row: res)
                users.emplace_back(hydrateUser(txn, row));

            return users;
        });
    }

    void User::updateLastLoggedInUser(const unsigned int userId) {
        Transactions::exec("User::updateLastLoggedInUser", [&](pqxx::work &txn) {
            txn.exec(
                pqxx::prepped{"update_user_last_login"},
                pqxx::params{userId}
            );
        });
    }

    void User::bootstrapSetAdminLinuxUID(const unsigned int linuxUid, const std::optional<unsigned int> updatedBy) {
        if (linuxUid == 0)
            throw std::runtime_error("admin cannot be assigned linux_uid 0");

        Transactions::exec("User::bootstrapSetAdminLinuxUID", [&](pqxx::work &txn) {
            txn.exec("SELECT set_config('vaulthalla.bootstrap', 'on', true)");

            const auto res = txn.exec(
                R"SQL(
                    UPDATE users
                    SET linux_uid = $1,
                        last_modified_by = $2,
                        updated_at = NOW()
                    WHERE name = 'admin'
                    RETURNING id
                )SQL",
                pqxx::params{linuxUid, updatedBy}
            );

            if (res.empty())
                throw std::runtime_error("admin user record not found");
        });
    }

    bool User::userExists(const std::string &name) {
        return Transactions::exec("User::userExists", [&](pqxx::work &txn) -> bool {
            return txn.exec(
                pqxx::prepped{"user_exists"},
                pqxx::params{name}
            ).one_field().as<bool>();
        });
    }

    bool User::adminUserExists() {
        return Transactions::exec("User::adminUserExists", [](pqxx::work &txn) -> bool {
            return txn.exec(
                pqxx::prepped{"admin_user_exists"}
            ).one_field().as<bool>();
        });
    }

    bool User::adminPasswordIsDefault() {
        return Transactions::exec("User::adminPasswordIsDefault", [](pqxx::work &txn) -> bool {
            const auto passwordHash = txn.exec(
                pqxx::prepped{"get_admin_password"}
            ).one_field().as<std::string>();

            return vh::crypto::hash::verifyPassword("vh!adm1n", passwordHash);
        });
    }
}
