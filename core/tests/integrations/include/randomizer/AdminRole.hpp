#pragma once

#include "randomizer/Permission.hpp"

#include <memory>

namespace vh::rbac::role { struct Admin; }
namespace vh::identities { struct User; struct Group; }

namespace vh::test::integration::randomizer {
    struct AdminRole {
        static void assignRandomRole(const std::shared_ptr<identities::User>& user);
        static void assignRandomPermissions(const std::shared_ptr<rbac::role::Admin>& role);

        static std::shared_ptr<rbac::role::Admin> getRandomRole();

        template<typename EnumT, typename SetT>
        static void randomizePerms(SetT& set) {
            set.permissions = Permission::random<typename SetT::Mask, EnumT>();
        }
    };
}
