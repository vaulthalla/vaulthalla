#pragma once

#include "CommandUsage.hpp"

#include <algorithm>
#include <string>
#include <variant>

namespace vh::protocols::shell {

// True when `key` (an option as parsed, without dashes) is one of `node`'s declared options or flags, including
// grouped ones. option_prefixes families are not "declared": the command validates those itself.
inline bool usageDeclaresOption(const CommandUsage& node, const std::string& key) {
    const auto inTokens = [&](const auto& entries) {
        return std::ranges::any_of(entries, [&](const auto& e) {
            return std::ranges::find(e.option_tokens, key) != e.option_tokens.end();
        });
    };
    const auto inFlags = [&](const auto& flags) {
        return std::ranges::any_of(flags, [&](const auto& f) { return std::ranges::find(f.aliases, key) != f.aliases.end(); });
    };
    if (inTokens(node.optional) || inTokens(node.required) || inFlags(node.optional_flags) || inFlags(node.required_flags))
        return true;
    for (const auto& group : node.groups)
        for (const auto& item : group.items) {
            const bool hit = std::visit([&](const auto& entry) {
                if constexpr (requires { entry.option_tokens; })
                    return std::ranges::find(entry.option_tokens, key) != entry.option_tokens.end();
                else
                    return std::ranges::find(entry.aliases, key) != entry.aliases.end();
            }, item);
            if (hit) return true;
        }
    return false;
}

}
