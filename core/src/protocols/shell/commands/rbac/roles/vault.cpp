#include "protocols/shell/commands/rbac.hpp"
#include "protocols/shell/types.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/permissionFlags.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "ops/Roles.hpp"
#include "rbac/role/Vault.hpp"

#include "UsageManager.hpp"
#include "CommandUsage.hpp"

#include <string>

namespace vh::protocols::shell::commands::rbac::roles::vault {
    // CLI syntax only: a numeric positional names a role by id, anything else by name.
    static ops::roles::Ref vaultRoleCliRef(const std::string& nameOrId) {
        if (const auto id = parseUInt(nameOrId)) return *id;
        return nameOrId;
    }

    static CommandResult handle_create(const CommandCall& call) {
        const auto usage = resolveUsage({"role", "vault", "create"});
        validatePositionals(call, usage);

        return runOp("role vault create", [&] {
            const auto from = optVal(call, usage->resolveOptional("inherit_from")->option_tokens);
            return ops::roles::createVaultRole(call.user, {
                .name = call.positionals[0],
                .description = optVal(call, usage->resolveOptional("description")->option_tokens).value_or(""),
                .from = from ? std::optional<ops::roles::Ref>(vaultRoleCliRef(*from)) : std::nullopt,
                .permissions = permissionEditFromFlags<vh::rbac::role::Vault>(call, usage)
            });
        }, [](const auto& role) { return "Role '" + role->name + "' created successfully\n" + role->toString(); });
    }

    static CommandResult handle_update(const CommandCall& call) {
        const auto usage = resolveUsage({"role", "vault", "update"});
        validatePositionals(call, usage);

        return runOp("role vault update", [&] {
            return ops::roles::updateVaultRole(call.user, {
                .role = vaultRoleCliRef(call.positionals[0]),
                .name = optVal(call, usage->resolveOptional("role_name")->option_tokens),
                .description = optVal(call, usage->resolveOptional("description")->option_tokens),
                .permissions = permissionEditFromFlags<vh::rbac::role::Vault>(call, usage)
            });
        }, [](const auto& role) { return "Role '" + role->name + "' updated successfully\n" + role->toString(); });
    }

    static CommandResult handle_delete(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "vault", "delete"}));
        return runOp("role vault delete",
            [&] { return ops::roles::removeVaultRole(call.user, vaultRoleCliRef(call.positionals[0])); },
            [](const auto& role) { return "Role '" + role->name + "' deleted successfully"; });
    }

    static CommandResult handle_info(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "vault", "info"}));
        return runOp("role vault info",
            [&] { return ops::roles::getVaultRole(call.user, vaultRoleCliRef(call.positionals[0])); },
            [](const auto& role) { return to_string(*role); });
    }

    static CommandResult handle_list(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "vault", "list"}));
        return runOp("role vault list",
            [&] { return ops::roles::listVaultRoles(call.user, parseListQuery(call)); },
            [](const auto& roles) { return to_string(roles); });
    }

    static bool is_vault_role_match(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"role", "vault", cmd}, input);
    }

    CommandResult handle_vault_roles(const CommandCall &call) {
        if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
            return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (is_vault_role_match("create", sub)) return handle_create(subcall);
        if (is_vault_role_match("update", sub)) return handle_update(subcall);
        if (is_vault_role_match("delete", sub)) return handle_delete(subcall);
        if (is_vault_role_match("info", sub)) return handle_info(subcall);
        if (is_vault_role_match("list", sub)) return handle_list(subcall);

        return invalid(call.constructFullArgs(), "Unknown vault roles subcommand: '" + std::string(sub) + "'");
    }
}
