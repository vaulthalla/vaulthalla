#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace vh::rbac::role { struct Admin; }

namespace vh::db::query::rbac::role::admin {

    struct Assignments {
        static void assign(uint32_t userId, uint32_t roleId);
        static void unassign(uint32_t userId);
        static bool exists(uint32_t userId);

        static std::shared_ptr<vh::rbac::role::Admin> get(uint32_t userId);
        static std::shared_ptr<vh::rbac::role::Admin> getByAssignmentId(uint32_t assignmentId);

        static std::vector<std::shared_ptr<vh::rbac::role::Admin>> listAll();
    };

}
