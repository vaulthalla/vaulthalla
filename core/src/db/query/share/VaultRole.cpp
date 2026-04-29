#include "db/query/share/VaultRole.hpp"

#include "db/Transactions.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/role/Vault.hpp"

#include <pqxx/pqxx>

#include <stdexcept>

namespace vh::db::query::share {
namespace {

void requireShareId(const std::string& shareId) {
    if (shareId.empty()) throw std::invalid_argument("Share id is required");
}

uint32_t upsertMapping(
    pqxx::work& txn,
    const std::string& shareId,
    const uint32_t vaultId,
    const uint32_t roleId
) {
    const auto res = txn.exec(
        pqxx::prepped{"share_vault_role_upsert_mapping"},
        pqxx::params{shareId, vaultId, roleId}
    );

    if (res.empty()) throw std::runtime_error("Failed to persist share vault role mapping");
    return res.one_row()["id"].as<uint32_t>();
}

void replaceOverrides(
    pqxx::work& txn,
    const uint32_t mappingId,
    const std::shared_ptr<vh::rbac::role::Vault>& role
) {
    txn.exec(pqxx::prepped{"share_vault_role_delete_overrides"}, pqxx::params{mappingId});

    for (const auto& override : role->fs.overrides) {
        auto permissionId = override.permission.id;
        if (permissionId == 0) {
            const auto permRes = txn.exec(
                pqxx::prepped{"share_vault_role_permission_id_by_name"},
                pqxx::params{override.permission.qualified_name}
            );
            if (permRes.empty())
                throw std::runtime_error("Share vault role override permission is not registered: " +
                                         override.permission.qualified_name);
            permissionId = permRes.one_row()["id"].as<uint32_t>();
        }

        txn.exec(
            pqxx::prepped{"share_vault_role_override_insert"},
            pqxx::params{
                mappingId,
                permissionId,
                override.glob_path(),
                override.enabled,
                vh::rbac::permission::to_string(override.effect)
            }
        );
    }
}

}

uint32_t VaultRole::upsertForShare(
    const std::string& shareId,
    const uint32_t vaultId,
    const std::shared_ptr<vh::rbac::role::Vault>& role
) {
    requireShareId(shareId);
    if (vaultId == 0) throw std::invalid_argument("Share vault id is required");

    return Transactions::exec("share::VaultRole::upsertForShare", [&](pqxx::work& txn) {
        const auto roleId = vh::db::query::rbac::role::Vault::upsert(txn, role);
        const auto mappingId = upsertMapping(txn, shareId, vaultId, roleId);
        replaceOverrides(txn, mappingId, role);
        return mappingId;
    });
}

std::shared_ptr<vh::rbac::role::Vault> VaultRole::getForShare(const std::string& shareId) {
    requireShareId(shareId);

    return Transactions::exec("share::VaultRole::getForShare", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Vault> {
        const auto roleRes = txn.exec(
            pqxx::prepped{"share_vault_role_get_by_share_id"},
            pqxx::params{shareId}
        );

        if (roleRes.empty()) return nullptr;

        const auto mappingId = roleRes.one_row()["share_vault_role_id"].as<uint32_t>();
        const auto overrideRes = txn.exec(
            pqxx::prepped{"share_vault_role_override_list_by_mapping_id"},
            pqxx::params{mappingId}
        );

        return std::make_shared<vh::rbac::role::Vault>(roleRes.one_row(), overrideRes);
    });
}

void VaultRole::removeForShare(const std::string& shareId) {
    requireShareId(shareId);

    Transactions::exec("share::VaultRole::removeForShare", [&](pqxx::work& txn) {
        txn.exec(
            pqxx::prepped{"share_vault_role_delete_by_share_id"},
            pqxx::params{shareId}
        );
    });
}

}
