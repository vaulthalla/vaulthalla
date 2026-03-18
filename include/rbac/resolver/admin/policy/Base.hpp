#pragma once

#include "rbac/resolver/ContextPolicy/Fwd.hpp"
#include "rbac/resolver/admin/ResolvedContext.hpp"

#include <memory>

namespace vh::identities { struct User; struct Group; }

namespace vh::rbac::resolver::admin {

    template<typename EnumT>
    struct ContextPolicy {
        static bool validate(const std::shared_ptr<identities::User>&,
                             const ResolvedContext&,
                             EnumT) {
            return true;
        }
    };

}
