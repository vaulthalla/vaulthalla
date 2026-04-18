#pragma once

#include "randomizer/Permission.hpp"

#include <cstdint>
#include <vector>
#include <memory>

namespace vh::rbac::role { struct Vault; }
namespace vh::identities { struct User; struct Group; }

namespace vh::test::integration::randomizer {
    struct VaultRole {
        static void assignRandomPermissions(const std::shared_ptr<rbac::role::Vault>& vRole);

        static std::shared_ptr<rbac::role::Vault> getRandomRole();

        template<typename IdentityT>
        static void assignRandomRole(const std::shared_ptr<IdentityT>& identity, const uint32_t vaultId) {
            identity->roles.vaults[vaultId] = getRandomRole();
        }

        template<typename EnumT, typename SetT>
        static void randomizePerms(SetT& set) {
            set.permissions = Permission::random<typename SetT::Mask, EnumT>();
        }
    };
}
