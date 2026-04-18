#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/types.hpp"
#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "CommandUsage.hpp"

namespace vh::protocols::shell {

std::shared_ptr<CommandUsage> resolveUsage(const std::vector<std::string>& path) {
    const auto usage = runtime::Deps::get().shellUsageManager->resolve(path);
    if (!usage) throw std::runtime_error("No usage found for '" + (path.empty() ? "root" : path[0]) + "'");
    return usage;
}

void validatePositionals(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    if (call.positionals.size() != usage->positionals.size())
        throw std::runtime_error("Invalid number of positionals for command '" + usage->primary() + "': expected " +
                                 std::to_string(usage->positionals.size()) + ", got " + std::to_string(call.positionals.size()));
}

}
