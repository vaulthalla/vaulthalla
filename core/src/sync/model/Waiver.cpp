#include "sync/model/Waiver.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

using namespace vh::sync::model;

Waiver::RoleContext Waiver::resolveOverridingRole() const {
    return std::visit(
        [](const auto& rolePtr) -> RoleContext {
            using T = std::decay_t<decltype(rolePtr)>;

            if (!rolePtr)
                throw std::runtime_error("Waiver::overridingRole contains null role pointer");

            if constexpr (std::is_same_v<T, std::shared_ptr<vh::rbac::role::Vault>>) {
                return RoleContext{
                    .id   = rolePtr->id,
                    .name = rolePtr->name,
                    .type = "vault"
                };
            } else if constexpr (std::is_same_v<T, std::shared_ptr<vh::rbac::role::Admin>>) {
                return RoleContext{
                    .id   = rolePtr->id,
                    .name = rolePtr->name,
                    .type = "admin"
                };
            } else {
                static_assert(std::is_same_v<T, void>, "Unhandled overridingRole variant type");
            }
        },
        overridingRole
    );
}
