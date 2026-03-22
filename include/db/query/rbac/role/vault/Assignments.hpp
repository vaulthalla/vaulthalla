#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vh::rbac::role { struct Vault; }

namespace vh::db::query::rbac::role::vault {

    struct Assignments {
        static void assign(uint32_t vaultId,
                           const std::string& subjectType,
                           uint32_t subjectId,
                           uint32_t roleId);

        static void unassign(uint32_t vaultId,
                             const std::string& subjectType,
                             uint32_t subjectId);

        static bool exists(uint32_t vaultId,
                           const std::string& subjectType,
                           uint32_t subjectId);

        static std::shared_ptr<vh::rbac::role::Vault> get(uint32_t vaultId,
                                                          const std::string& subjectType,
                                                          uint32_t subjectId);

        static std::shared_ptr<vh::rbac::role::Vault> getByAssignmentId(uint32_t assignmentId);

        static std::vector<std::shared_ptr<vh::rbac::role::Vault>> listForVault(uint32_t vaultId);

        static std::vector<std::shared_ptr<vh::rbac::role::Vault>> listForSubject(const std::string& subjectType,
                                                                                   uint32_t subjectId);

        static std::vector<std::shared_ptr<vh::rbac::role::Vault>> listAll();

        static uint32_t countAssignmentsForRole(uint32_t role_id);
    };

}
