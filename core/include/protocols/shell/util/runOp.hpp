#pragma once

#include "ops/Error.hpp"
#include "protocols/shell/types.hpp"
#include "protocols/shell/util/argsHelpers.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace vh::protocols::shell {

// Runs an ops:: operation from a CLI handler and formats its result. An ops::Error (a refusal: denied, not found,
// invalid, conflict) becomes the usual usage-error result, exit 2 with "<prefix>: <reason>". Anything else is a
// fault and propagates; the shell server reports it as exit 1, as it always has.
template<class Op, class Format>
CommandResult runOp(const std::string_view prefix, Op&& op, Format&& format) {
    using Result = std::invoke_result_t<Op>;
    if constexpr (std::is_void_v<Result>) {
        try {
            std::invoke(std::forward<Op>(op));
        } catch (const ops::Error& e) {
            return invalid(std::string(prefix) + ": " + e.what());
        }
        return ok(std::invoke(std::forward<Format>(format)));
    } else {
        std::optional<Result> result;
        try {
            result.emplace(std::invoke(std::forward<Op>(op)));
        } catch (const ops::Error& e) {
            return invalid(std::string(prefix) + ": " + e.what());
        }
        return ok(std::invoke(std::forward<Format>(format), *result));
    }
}

}
