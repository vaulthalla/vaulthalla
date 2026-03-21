#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <functional>
#include <sstream>

namespace vh::rbac::permission {
    enum class FlagOperation : uint8_t {
        Grant,
        Revoke
    };

    template<typename Enum>
    struct FlagBinding {
        std::string flag;
        Enum permission;
        FlagOperation operation{};
        std::string_view slug;
    };

    struct ResolvedFlagBinding {
        std::string flag;
        std::function<void()> apply;
    };

    struct FlagResolutionResult {
        std::vector<std::string> errors;

        [[nodiscard]] bool ok() const noexcept { return errors.empty(); }

        [[nodiscard]] std::string toString() const {
            if (errors.empty()) return {};

            std::ostringstream oss;
            oss << "Failed to update permissions from flags:\n";
            for (const auto &error : errors)
                oss << "  - " << error << '\n';
            return oss.str();
        }
    };
}
