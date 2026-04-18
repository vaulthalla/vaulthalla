#pragma once

#include "rbac/permission/vault/Filesystem.hpp"

namespace vh::rbac::resolver {
    template<typename EnumT>
    struct VaultResolverMode {
        static constexpr bool policy_only = false;
    };

    template<>
    struct VaultResolverMode<permission::vault::FilesystemAction> {
        static constexpr bool policy_only = true;
    };
}
