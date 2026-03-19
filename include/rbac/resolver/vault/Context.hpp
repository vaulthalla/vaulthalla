#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>

namespace vh::identities {
    struct User;
}

namespace vh::fs::model {
    struct Entry;
}

namespace vh::rbac::resolver::vault {
    template<typename EnumT>
    struct Context {
        static_assert(std::is_enum_v<EnumT>, "vh::rbac::vault::Context<EnumT>: EnumT must be an enum type");

        std::shared_ptr<identities::User> user;
        std::optional<EnumT> permission{std::nullopt};
        std::vector<EnumT> permissions{};
        std::optional<std::string> target_subject_type{std::nullopt};
        std::optional<uint32_t> target_subject_id{std::nullopt};
        std::optional<uint32_t> vault_id{std::nullopt};
        std::optional<std::filesystem::path> path{std::nullopt};
        std::optional<std::shared_ptr<vh::fs::model::Entry> > entry{std::nullopt};

        [[nodiscard]] bool isValid() const {
            return !!user && (permission || !permissions.empty());
        }
    };
}
