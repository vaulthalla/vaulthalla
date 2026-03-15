#pragma once

#include "rbac/vault/resolver/ContextPolicy/Fwd.hpp"

#include <memory>

namespace vh::identities { struct User; struct Group; }

namespace vh::rbac::vault::resolver {

    template<typename EnumT>
    struct ContextPolicy {
        static bool validate(const std::shared_ptr<identities::User>&,
                             const ResolvedVaultContext&,
                             EnumT) {
            return true;
        }
    };

}
