#pragma once

#include <memory>
#include <unordered_map>
#include <cstdint>
#include <pqxx/pqxx>

namespace vh {
    namespace rbac {
        namespace role { struct Vault; }
        namespace permission::admin { struct VaultGlobals; }
    }

    namespace identities { struct User; struct Group; }

    namespace db::query::identities {
        std::shared_ptr<vh::identities::User> hydrateUser(pqxx::work& txn, const pqxx::row& userRow);

        std::shared_ptr<vh::identities::Group> hydrateGroup(pqxx::work& txn, const pqxx::row& groupRow);

        void upsertUserRoles(pqxx::work& txn, const std::shared_ptr<vh::identities::User>& user);

        void upsertVaultRoles(pqxx::work& txn, const std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>>& vRoles,
                                     const std::string &subjectType, uint32_t subjectId);

        void upsertGlobalVRoles(pqxx::work& txn, const rbac::permission::admin::VaultGlobals &vGlobal, uint32_t userId);

        void upsertAdminRoleAssignment(pqxx::work& txn, uint32_t userId, uint32_t roleId);

        std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>> getVaultRoles(pqxx::work &txn, const std::string& subjectType, uint32_t subjectId);

        std::vector<std::shared_ptr<vh::identities::Group>> getUserGroups(pqxx::work& txn, uint32_t userId);
    }
}
