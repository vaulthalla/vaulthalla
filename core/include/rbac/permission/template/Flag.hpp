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
}
