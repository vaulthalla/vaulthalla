#pragma once

#include <memory>
#include <optional>
#include <type_traits>

namespace vh::identities { struct User; }

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

        [[nodiscard]] bool isValid() const {
            return !!user && (permission || !permissions.empty());
        }
    };

}
