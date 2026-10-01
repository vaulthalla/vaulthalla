#include "protocols/shell/commands/rbac.hpp"
#include "protocols/shell/types.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/permissionFlags.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "ops/Roles.hpp"
#include "rbac/role/Admin.hpp"

#include "UsageManager.hpp"
#include "CommandUsage.hpp"

#include <string>

namespace vh::protocols::shell::commands::rbac::roles::admin {
    // CLI syntax only: a numeric positional names a role by id, anything else by name.
    static ops::roles::Ref adminRoleCliRef(const std::string& nameOrId) {
        if (const auto id = parseUInt(nameOrId)) return *id;
        return nameOrId;
    }

    static CommandResult handle_create(const CommandCall& call) {
        const auto usage = resolveUsage({"role", "admin", "create"});
        validatePositionals(call, usage);

        return runOp("role admin create", [&] {
            const auto from = optVal(call, usage->resolveOptional("inherit_from")->option_tokens);
            return ops::roles::createAdminRole(call.user, {
                .name = call.positionals[0],
                .description = optVal(call, usage->resolveOptional("description")->option_tokens).value_or(""),
                .from = from ? std::optional<ops::roles::Ref>(adminRoleCliRef(*from)) : std::nullopt,
                .permissions = permissionEditFromFlags<vh::rbac::role::Admin>(call, usage)
            }, "shell");
        }, [](const auto& role) { return "Role '" + role->name + "' created successfully\n" + role->toString(); });
    }

    static CommandResult handle_update(const CommandCall& call) {
        const auto usage = resolveUsage({"role", "admin", "update"});
        validatePositionals(call, usage);

        return runOp("role admin update", [&] {
            return ops::roles::updateAdminRole(call.user, {
                .role = adminRoleCliRef(call.positionals[0]),
                .name = optVal(call, usage->resolveOptional("role_name")->option_tokens),
                .description = optVal(call, usage->resolveOptional("description")->option_tokens),
                .permissions = permissionEditFromFlags<vh::rbac::role::Admin>(call, usage)
            }, "shell");
        }, [](const auto& role) { return "Role '" + role->name + "' updated successfully\n" + role->toString(); });
    }

    static CommandResult handle_delete(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "admin", "delete"}));
        return runOp("role admin delete",
            [&] { return ops::roles::removeAdminRole(call.user, adminRoleCliRef(call.positionals[0]), "shell"); },
            [](const auto& role) { return "Role '" + role->name + "' deleted successfully"; });
    }

    static CommandResult handle_info(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "admin", "info"}));
        return runOp("role admin info",
            [&] { return ops::roles::getAdminRole(call.user, adminRoleCliRef(call.positionals[0])); },
            [](const auto& role) { return to_string(*role); });
    }

    static CommandResult handle_list(const CommandCall& call) {
        validatePositionals(call, resolveUsage({"role", "admin", "list"}));
        return runOp("role admin list",
            [&] { return ops::roles::listAdminRoles(call.user, parseListQuery(call)); },
            [](const auto& roles) { return to_string(roles); });
    }

    static bool is_admin_role_match(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"role", "admin", cmd}, input);
    }

    CommandResult handle_admin_roles(const CommandCall &call) {
        if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
            return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (is_admin_role_match("create", sub)) return handle_create(subcall);
        if (is_admin_role_match("update", sub)) return handle_update(subcall);
        if (is_admin_role_match("delete", sub)) return handle_delete(subcall);
        if (is_admin_role_match("info", sub)) return handle_info(subcall);
        if (is_admin_role_match("list", sub)) return handle_list(subcall);

        return invalid(call.constructFullArgs(), "Unknown admin roles subcommand: '" + std::string(sub) + "'");
    }
}
