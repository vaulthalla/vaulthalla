#include "db/query/rbac/role/vault/Global.hpp"
#include "db/Transactions.hpp"
#include "db/template/Paginate.hpp"
#include "rbac/role/vault/Global.hpp"

#include <pqxx/pqxx>
#include <stdexcept>

using namespace vh::db::query::rbac::roles::vault;

using GlobalVaultRole = vh::rbac::role::vault::Global;
using GlobalVaultRolePtr = std::shared_ptr<GlobalVaultRole>;

void Global::upsert(const GlobalVaultRolePtr& role) {
    if (!role) throw std::invalid_argument("roles::vault::Global::upsert received null role");

    Transactions::exec("roles::vault::Global::upsert", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_upsert"},
            pqxx::params{
                role->user_id,
                role->template_role_id,
                role->enforce_template,
                role->scope,
                role->fs.files.toBitString(),
                role->fs.directories.toBitString(),
                role->sync.toBitString(),
                role->roles.toBitString()
            }
        );
    });
}

void Global::add(const GlobalVaultRolePtr& role) {
    if (!role) throw std::invalid_argument("roles::vault::Global::add received null role");

    Transactions::exec("roles::vault::Global::add", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_insert"},
            pqxx::params{
                role->user_id,
                role->template_role_id,
                role->enforce_template,
                role->scope,
                role->fs.files.toBitString(),
                role->fs.directories.toBitString(),
                role->sync.toBitString(),
                role->roles.toBitString()
            }
        );
    });
}

void Global::update(const GlobalVaultRolePtr& role) {
    if (!role) throw std::invalid_argument("roles::vault::Global::update received null role");

    Transactions::exec("roles::vault::Global::update", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_update"},
            pqxx::params{
                role->user_id,
                role->scope,
                role->template_role_id,
                role->enforce_template,
                role->fs.files.toBitString(),
                role->fs.directories.toBitString(),
                role->sync.toBitString(),
                role->roles.toBitString()
            }
        );
    });
}

void Global::remove(const unsigned int userId, const std::string& scope) {
    Transactions::exec("roles::vault::Global::remove", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_delete"},
            pqxx::params{userId, scope}
        );
    });
}

void Global::removeAllForUser(const unsigned int userId) {
    Transactions::exec("roles::vault::Global::removeAllForUser", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"user_global_vault_policy_delete_all_for_user"},
            pqxx::params{userId}
        );
    });
}

GlobalVaultRolePtr Global::get(const unsigned int userId, const std::string& scope) {
    return Transactions::exec("roles::vault::Global::get", [&](pqxx::work& txn) -> GlobalVaultRolePtr {
        const auto res = txn.exec(
            pqxx::prepped{"user_global_vault_policy_get"},
            pqxx::params{userId, scope}
        );

        if (res.empty()) return nullptr;
        return std::make_shared<GlobalVaultRole>(res.one_row());
    });
}

bool Global::exists(const unsigned int userId, const std::string& scope) {
    return Transactions::exec("roles::vault::Global::exists", [&](pqxx::work& txn) -> bool {
        const auto res = txn.exec(
            pqxx::prepped{"user_global_vault_policy_exists"},
            pqxx::params{userId, scope}
        );

        if (res.empty()) return false;
        return res.one_row()[0].as<bool>();
    });
}

std::vector<GlobalVaultRolePtr> Global::listByUser(const unsigned int userId, model::ListQueryParams&& params) {
    return Transactions::exec("roles::vault::Global::listByUser", [&](pqxx::work& txn) -> std::vector<GlobalVaultRolePtr> {
        const auto res = txn.exec(
            pqxx::prepped{"user_global_vault_policy_list_by_user"},
            pqxx::params{userId}
        );

        std::vector<GlobalVaultRolePtr> roles;
        roles.reserve(res.size());

        for (const auto& row : res)
            roles.emplace_back(std::make_shared<GlobalVaultRole>(row));

        return template_::paginate(std::move(roles), params);
    });
}

std::vector<GlobalVaultRolePtr> Global::list(model::ListQueryParams&& params) {
    return Transactions::exec("roles::vault::Global::list", [&](pqxx::work& txn) -> std::vector<GlobalVaultRolePtr> {
        const auto res = txn.exec(pqxx::prepped{"user_global_vault_policy_list_all"});

        std::vector<GlobalVaultRolePtr> roles;
        roles.reserve(res.size());

        for (const auto& row : res)
            roles.emplace_back(std::make_shared<GlobalVaultRole>(row));

        return template_::paginate(std::move(roles), params);
    });
}
