#pragma once

#include "randomizer/Permission.hpp"
#include "identities/Fwd.hpp"
#include "rbac/Fwd.hpp"

#include <memory>

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
