#pragma once

#include <memory>
#include <optional>
#include <type_traits>
#include "identities/Fwd.hpp"
#include "vault/Fwd.hpp"

#include <cstdint>
#include <vector>

namespace vh::rbac::resolver::admin {

    enum class Entity { User, Group, Admin };

    template<typename EnumT>
    struct Context {
        static_assert(std::is_enum_v<EnumT>, "vh::rbac::vault::Context<EnumT>: EnumT must be an enum type");

        std::shared_ptr<identities::User> user;
        std::optional<EnumT> permission{std::nullopt};
        std::vector<EnumT> permissions{};
        std::optional<Entity> identity{};
        std::optional<uint32_t> api_key_id{};
        std::optional<uint32_t> target_user_id{};
        std::optional<uint32_t> vault_id{std::nullopt};
        // A vault without a live engine (one pending deletion, #162): its owner decides the scope instead of the
        // engine's. Takes precedence over vault_id.
        std::shared_ptr<::vh::vault::model::Vault> vault{};

        [[nodiscard]] bool isValid() const {
            return !!user && (permission || !permissions.empty());
        }
    };

}
