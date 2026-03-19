#include "db/query/rbac/role/Vault.hpp"
#include "db/Transactions.hpp"
#include "db/template/Paginate.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <pqxx/pqxx>
#include <stdexcept>

using VaultRole = vh::rbac::role::Vault;
using VaultRolePtr = std::shared_ptr<VaultRole>;

namespace vh::db::query::rbac::role {
    unsigned int Vault::upsert(const VaultRolePtr& role) {
        if (!role) throw std::invalid_argument("role::Vault::upsert received null role");

        return Transactions::exec("role::Vault::upsert", [&](pqxx::work& txn) {
            if (role->id != 0) {
                txn.exec(
                    pqxx::prepped{"vault_role_upsert"},
                    pqxx::params{
                        role->id,
                        role->name,
                        role->description,
                        role->fs.files.toBitString(),
                        role->fs.directories.toBitString(),
                        role->sync.toBitString(),
                        role->roles.toBitString(),
                    }
                );

                return role->id;
            }

            const auto res = txn.exec(
                pqxx::prepped{"vault_role_upsert_by_name"},
                pqxx::params{
                    role->name,
                    role->description,
                    role->fs.files.toBitString(),
                    role->fs.directories.toBitString(),
                    role->sync.toBitString(),
                    role->roles.toBitString(),
                }
            );

            if (res.empty()) throw std::runtime_error("role::Vault::upsert failed to return row");
            return res.one_row()["id"].as<unsigned int>();
        });
    }

    void Vault::remove(unsigned int id) {
        Transactions::exec("role::Vault::remove(id)", [&](pqxx::work& txn) {
            txn.exec(pqxx::prepped{"vault_role_delete"}, pqxx::params{id});
        });
    }

    void Vault::remove(const VaultRolePtr& role) {
        if (!role) return;
        remove(role->id);
    }

    VaultRolePtr Vault::get(unsigned int id) {
        return Transactions::exec("role::Vault::get(id)", [&](pqxx::work& txn) -> VaultRolePtr {
            const auto roleRes = txn.exec(
                pqxx::prepped{"vault_role_get_by_id"},
                pqxx::params{id}
            );

            if (roleRes.empty()) return nullptr;

            return std::make_shared<VaultRole>(roleRes.one_row());
        });
    }

    VaultRolePtr Vault::get(const std::string& name) {
        return Transactions::exec("role::Vault::get(name)", [&](pqxx::work& txn) -> VaultRolePtr {
            const auto roleRes = txn.exec(
                pqxx::prepped{"vault_role_get_by_name"},
                pqxx::params{name}
            );

            if (roleRes.empty()) return nullptr;

            return std::make_shared<VaultRole>(roleRes.one_row());
        });
    }

    bool Vault::exists(unsigned int id) {
        return Transactions::exec("role::Vault::exists(id)", [&](pqxx::work& txn) -> bool {
            const auto res = txn.exec(
                pqxx::prepped{"vault_role_exists_by_id"},
                pqxx::params{id}
            );

            if (res.empty()) return false;
            return res.one_row()[0].as<bool>();
        });
    }

    bool Vault::exists(const std::string& name) {
        return Transactions::exec("role::Vault::exists(name)", [&](pqxx::work& txn) -> bool {
            const auto res = txn.exec(
                pqxx::prepped{"vault_role_exists_by_name"},
                pqxx::params{name}
            );

            if (res.empty()) return false;
            return res.one_row()[0].as<bool>();
        });
    }

    std::vector<VaultRolePtr> Vault::list(model::ListQueryParams&& params) {
        return Transactions::exec("role::Vault::list", [&](pqxx::work& txn) -> std::vector<VaultRolePtr> {
            std::vector<VaultRolePtr> roles;

            const auto roleRes = txn.exec(pqxx::prepped{"vault_role_list_all"});

            roles.reserve(roleRes.size());

            for (const auto& row : roleRes) roles.emplace_back(std::make_shared<VaultRole>(row));

            return template_::paginate(std::move(roles), params);
        });
    }
}
