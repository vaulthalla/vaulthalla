#pragma once

#include <concepts>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace vh::rbac::permission {
    template<typename T>
    concept HasDescriptionContext =
            requires(const T &t)
            {
                { t.descriptionObject() } -> std::convertible_to<std::string_view>;
            };

    template<typename T>
    [[nodiscard]] std::string buildPermissionDescription(
        const T &object,
        std::string_view description,
        std::string_view descriptionPrefix = {}
    ) {
        std::string result;

        if constexpr (HasDescriptionContext<std::remove_cvref_t<T> >) {
            const std::string_view ctx = object.descriptionObject();

            result = std::vformat(
                std::string(description),
                std::make_format_args(ctx)
            );
        } else {
            result = std::string(description);

            if (result.find('{') != std::string::npos || result.find('}') != std::string::npos)
                throw std::runtime_error("Permission description requires descriptionObject() context");
        }

        if (!descriptionPrefix.empty())
            result.insert(0, descriptionPrefix);

        return result;
    }
}
