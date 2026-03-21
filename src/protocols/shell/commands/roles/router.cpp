#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "db/query/rbac/Permission.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "runtime/Deps.hpp"
#include "usage/include/UsageManager.hpp"
#include "CommandUsage.hpp"

using namespace vh;
using namespace vh::protocols::shell;

static void parseFlag(const CommandCall& call, const std::string& flag, uint16_t& permissions, const unsigned int bitPosition) {
    if (hasFlag(call, flag) || hasFlag(call, "allow-" + flag)) permissions |= (1 << bitPosition);
    else if (hasFlag(call, "deny-" + flag)) permissions &= ~(1 << bitPosition);
}

static uint16_t parseUserRolePermissions(const CommandCall& call, uint16_t permissions = 0) {
    parseFlag(call, "manage-encryption-keys", permissions, 0);
    parseFlag(call, "manage-admins", permissions, 1);
    parseFlag(call, "manage-users", permissions, 2);
    parseFlag(call, "manage-groups", permissions, 3);
    parseFlag(call, "manage-roles", permissions, 4);
    parseFlag(call, "manage-settings", permissions, 5);
    parseFlag(call, "manage-vaults", permissions, 6);
    parseFlag(call, "manage-api-keys", permissions, 7);
    parseFlag(call, "audit-log-access", permissions, 8);
    parseFlag(call, "create-vaults", permissions, 9);

    return permissions;
}

static uint16_t parseVaultRolePermissions(const CommandCall& call, uint16_t permissions = 0) {
    parseFlag(call, "manage-vault", permissions, 0);
    parseFlag(call, "manage-access", permissions, 1);
    parseFlag(call, "manage-tags", permissions, 2);
    parseFlag(call, "manage-metadata", permissions, 3);
    parseFlag(call, "manage-versions", permissions, 4);
    parseFlag(call, "manage-file-locks", permissions, 5);
    parseFlag(call, "share", permissions, 6);
    parseFlag(call, "sync", permissions, 7);
    parseFlag(call, "create", permissions, 8);
    parseFlag(call, "download", permissions, 9);
    parseFlag(call, "delete", permissions, 10);
    parseFlag(call, "rename", permissions, 11);
    parseFlag(call, "move", permissions, 12);
    parseFlag(call, "list", permissions, 13);

    return permissions;
}

static bool isRoleMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"role", cmd}, input);
}

static CommandResult handle_role(const CommandCall& call) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    if (call.positionals.empty()) return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    return invalid(call.constructFullArgs(), "Unknown roles subcommand: '" + std::string(sub) + "'");
}

void commands::registerRoleCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("role"), handle_role);
}
