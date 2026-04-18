#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/rbac.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"

using namespace vh;

namespace vh::protocols::shell::commands::rbac::roles {
    static bool isRoleMatch(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"role", cmd}, input);
    }

    static CommandResult handle_role(const CommandCall& call) {
        if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
            return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (isRoleMatch("admin", sub)) return admin::handle_admin_roles(subcall);
        if (isRoleMatch("vault", sub)) return vault::handle_vault_roles(subcall);

        return invalid(call.constructFullArgs(), "Unknown roles subcommand: '" + std::string(sub) + "'");
    }

    void registerCommands(const std::shared_ptr<Router>& r) {
        const auto usageManager = runtime::Deps::get().shellUsageManager;
        r->registerCommand(usageManager->resolve("role"), handle_role);
    }
}
