#include "db/query/rbac/role/vault/Assignments.hpp"

#include "db/Transactions.hpp"
#include "rbac/role/Vault.hpp"

namespace {

    std::shared_ptr<vh::rbac::role::Vault> makeAssignedVaultRole(pqxx::work& txn, const pqxx::row& row) {
        const auto roleId = row["assignment_id"].as<uint32_t>();

        const auto overridesRes = txn.exec(
            pqxx::prepped{"vault_permission_override_list_by_assignment_id"},
            pqxx::params{roleId}
        );

        return std::make_shared<vh::rbac::role::Vault>(row, overridesRes);
    }

}

namespace vh::db::query::rbac::role::vault {

    void Assignments::assign(const std::shared_ptr<vh::rbac::role::Vault> &role) {
        Transactions::exec("role::vault::Assignments::assign(role)", [&](pqxx::work& txn) {
            const auto res = txn.exec(
                pqxx::prepped{"vault_role_assignment_upsert"},
                pqxx::params{role->assignment->vault_id, role->assignment->subject_type, role->assignment->subject_id, role->id}
            );

            if (res.empty()) throw std::runtime_error("Failed to assign vault role - no result returned");
            role->assignment_id = res.one_row()["id"].as<uint32_t>();

            for (const auto& override : role->fs.overrides) {
                txn.exec(
                    pqxx::prepped{"upsert_vault_permission_override"},
                    pqxx::params{role->assignment_id, override.permission.id, override.glob_path(), override.enabled, vh::rbac::permission::to_string(override.effect)}
                );
            }
        });
    }

    uint32_t Assignments::assign(const uint32_t vaultId,
                                 const std::string& subjectType,
                                 const uint32_t subjectId,
                                 const uint32_t roleId) {
        return Transactions::exec("role::vault::Assignments::assign(vaultId, subjectType, subjectId, roleId)", [&](pqxx::work& txn) {
            const auto res = txn.exec(
                pqxx::prepped{"vault_role_assignment_upsert"},
                pqxx::params{vaultId, subjectType, subjectId, roleId}
            );

            if (res.empty()) throw std::runtime_error("Failed to assign vault role - no result returned");
            return res.one_row()["id"].as<uint32_t>();
        });
    }

    void Assignments::unassign(const uint32_t vaultId,
                               const std::string& subjectType,
                               const uint32_t subjectId) {
        Transactions::exec("role::vault::Assignments::unassign(vaultId, subjectType, subjectId)", [&](pqxx::work& txn) {
            txn.exec(
                pqxx::prepped{"vault_role_assignment_delete"},
                pqxx::params{vaultId, subjectType, subjectId}
            );
        });
    }

    bool Assignments::exists(const uint32_t vaultId,
                             const std::string& subjectType,
                             const uint32_t subjectId) {
        return Transactions::exec("role::vault::Assignments::exists(vaultId, subjectType, subjectId)", [&](pqxx::work& txn) -> bool {
            const auto result = txn.exec(
                pqxx::prepped{"vault_role_assignment_exists"},
                pqxx::params{vaultId, subjectType, subjectId}
            );

            if (result.empty()) return false;
            return result[0][0].as<bool>();
        });
    }

    std::shared_ptr<vh::rbac::role::Vault> Assignments::get(const uint32_t vaultId,
                         const std::string& subjectType,
                         const uint32_t subjectId) {
        return Transactions::exec("role::vault::Assignments::get(vaultId, subjectType, subjectId)", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Vault> {
            const auto assignmentRes = txn.exec(
                pqxx::prepped{"vault_role_assignment_get"},
                pqxx::params{vaultId, subjectType, subjectId}
            );

            if (assignmentRes.empty()) return nullptr;

            return makeAssignedVaultRole(txn, assignmentRes.one_row());
        });
    }

    std::shared_ptr<vh::rbac::role::Vault> Assignments::getByAssignmentId(const uint32_t assignmentId) {
        return Transactions::exec("role::vault::Assignments::getByAssignmentId(assignmentId)", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Vault> {
            const auto assignmentRes = txn.exec(
                pqxx::prepped{"vault_role_assignment_get_by_id"},
                pqxx::params{assignmentId}
            );

            if (assignmentRes.empty()) return nullptr;

            return makeAssignedVaultRole(txn, assignmentRes.one_row());
        });
    }

    std::vector<std::shared_ptr<vh::rbac::role::Vault>> Assignments::listForVault(const uint32_t vaultId) {
        return Transactions::exec("role::vault::Assignments::listForVault(vaultId)", [&](pqxx::work& txn) -> std::vector<std::shared_ptr<vh::rbac::role::Vault>> {
            const auto assignmentRes = txn.exec(
                pqxx::prepped{"vault_role_assignment_list_by_vault"},
                pqxx::params{vaultId}
            );

            std::vector<std::shared_ptr<vh::rbac::role::Vault>> roles;
            roles.reserve(assignmentRes.size());

            for (const auto& row : assignmentRes)
                roles.emplace_back(makeAssignedVaultRole(txn, row));

            return roles;
        });
    }

    std::vector<std::shared_ptr<vh::rbac::role::Vault>> Assignments::listForSubject(const std::string& subjectType,
                                     const uint32_t subjectId) {
        return Transactions::exec("role::vault::Assignments::listForSubject(subjectType, subjectId)", [&](pqxx::work& txn) -> std::vector<std::shared_ptr<vh::rbac::role::Vault>> {
            const auto assignmentRes = txn.exec(
                pqxx::prepped{"vault_role_assignment_list_by_subject"},
                pqxx::params{subjectType, subjectId}
            );

            std::vector<std::shared_ptr<vh::rbac::role::Vault>> roles;
            roles.reserve(assignmentRes.size());

            for (const auto& row : assignmentRes)
                roles.emplace_back(makeAssignedVaultRole(txn, row));

            return roles;
        });
    }

    std::vector<std::shared_ptr<vh::rbac::role::Vault>> Assignments::listAll() {
        return Transactions::exec("role::vault::Assignments::listAll()", [&](pqxx::work& txn) -> std::vector<std::shared_ptr<vh::rbac::role::Vault>> {
            const auto assignmentRes = txn.exec(
                pqxx::prepped{"vault_role_assignment_list_all"},
                pqxx::params{}
            );

            std::vector<std::shared_ptr<vh::rbac::role::Vault>> roles;
            roles.reserve(assignmentRes.size());

            for (const auto& row : assignmentRes)
                roles.emplace_back(makeAssignedVaultRole(txn, row));

            return roles;
        });
    }

    uint32_t Assignments::countAssignmentsForRole(uint32_t role_id) {
        return Transactions::exec("role::vault::Assignments::countAssignmentsForRole(role_id)", [&](pqxx::work& txn) -> uint32_t {
            const auto result = txn.exec(
                pqxx::prepped{"count_vault_role_assignments_by_role_id"},
                pqxx::params{role_id}
            );

            if (result.empty()) return 0;
            return result.one_field().as<uint32_t>();
        });
    }

}
