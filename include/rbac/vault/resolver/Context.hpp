#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>

namespace vh::identities { struct User; }

namespace vh::rbac::vault {

    template<typename EnumT>
    struct Context {
        static_assert(std::is_enum_v<EnumT>, "vh::rbac::vault::Context<EnumT>: EnumT must be an enum type");

        std::shared_ptr<identities::User> user;
        EnumT permission{};
        std::optional<uint32_t> vault_id{std::nullopt};
        std::optional<std::filesystem::path> path{std::nullopt};

        [[nodiscard]] bool isValid() const {
            return !!user;
        }
    };

}
