#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/role/vault/Global.hpp"

using namespace vh;
using namespace vh::protocols::shell;

static CommandResult handle_permission(const CommandCall& call) {
    if (call.positionals.empty() || call.positionals.size() > 1) return usage(call.constructFullArgs());
    const std::string_view sub = call.positionals[0];
    if (sub == "admin" || sub == "a") return ok(rbac::role::Admin::usage());
    if (sub == "vault" || sub == "v") return ok(rbac::role::Vault::usage());
    if (sub == "vault-global" || sub == "vg") return ok(rbac::role::vault::Global::usage());
    return invalid(call.constructFullArgs(), "Unknown permission subcommand: '" + std::string(sub) + "'");
}

void commands::registerPermissionCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("permission"), handle_permission);
}
