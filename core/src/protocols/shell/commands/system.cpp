#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

#include <version.h>

using namespace vh;
using namespace vh::protocols::shell;

static CommandResult handle_help(const CommandCall&) { return usage(); }

static CommandResult handle_version(const CommandCall&) {
    return {0, "Vaulthalla v" + std::string(VH_VERSION), ""};
}

void commands::registerSystemCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("help"), handle_help);
    r->registerCommand(usageManager->resolve("version"), handle_version);
}
