#pragma once

namespace vh::rbac::resolver {
    enum class Domain {
        APIKey,
        Identity,
        Vault
    };

    template<typename EnumT>
    struct VaultResolverTraits;

    template<typename EnumT>
    struct AdminResolverTraits;
}
