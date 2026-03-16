#include "db/query/rbac/role/admin/Assignments.hpp"

#include "db/Transactions.hpp"
#include "rbac/role/Admin.hpp"

namespace vh::db::query::rbac::role::admin {

    void Assignments::assign(const uint32_t userId, const uint32_t roleId) {
        Transactions::exec("role::admin::Assignments::assign(userId, roleId)", [&](pqxx::work& txn) {
            txn.exec(
                pqxx::prepped{"admin_role_assignment_upsert"},
                pqxx::params{userId, roleId}
            );
        });
    }

    void Assignments::unassign(const uint32_t userId) {
        Transactions::exec("role::admin::Assignments::unassign(userId)", [&](pqxx::work& txn) {
            txn.exec(
                pqxx::prepped{"admin_role_assignment_delete_by_user_id"},
                pqxx::params{userId}
            );
        });
    }

    bool Assignments::exists(const uint32_t userId) {
        return Transactions::exec("role::admin::Assignments::exists(userId)", [&](pqxx::work& txn) -> bool {
            const auto result = txn.exec(
                pqxx::prepped{"admin_role_assignment_exists_by_user_id"},
                pqxx::params{userId}
            );

            if (result.empty()) return false;
            return result[0][0].as<bool>();
        });
    }

    std::shared_ptr<vh::rbac::role::Admin> Assignments::get(const uint32_t userId) {
        return Transactions::exec("role::admin::Assignments::get(userId)", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Admin> {
            const auto result = txn.exec(
                pqxx::prepped{"admin_role_assignment_get_by_user_id"},
                pqxx::params{userId}
            );

            if (result.empty()) return nullptr;

            return std::make_shared<vh::rbac::role::Admin>(result.one_row());
        });
    }

    std::shared_ptr<vh::rbac::role::Admin> Assignments::getByAssignmentId(const uint32_t assignmentId) {
        return Transactions::exec("role::admin::Assignments::getByAssignmentId(assignmentId)", [&](pqxx::work& txn) -> std::shared_ptr<vh::rbac::role::Admin> {
            const auto result = txn.exec(
                pqxx::prepped{"admin_role_assignment_get_by_assignment_id"},
                pqxx::params{assignmentId}
            );

            if (result.empty()) return nullptr;

            return std::make_shared<vh::rbac::role::Admin>(result.one_row());
        });
    }

    std::vector<std::shared_ptr<vh::rbac::role::Admin>> Assignments::listAll() {
        return Transactions::exec("role::admin::Assignments::listAll()", [&](pqxx::work& txn) {
            const auto result = txn.exec(
                pqxx::prepped{"admin_role_assignment_list_all"},
                pqxx::params{}
            );

            std::vector<std::shared_ptr<vh::rbac::role::Admin>> roles;
            roles.reserve(result.size());

            for (const auto& row : result)
                roles.emplace_back(std::make_shared<vh::rbac::role::Admin>(row));

            return roles;
        });
    }

}
