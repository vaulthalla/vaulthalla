#include "protocols/shell/commands/rbac.hpp"
#include "protocols/shell/types.hpp"

#include "identities/User.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/resolver/permission/all.hpp"

#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"

#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "CommandUsage.hpp"
#include "usages.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "rbac/resolver/Admin.hpp"

using namespace vh::rbac;

namespace vh::protocols::shell::commands::rbac::roles::vault {
    static CommandResult handle_create(const CommandCall& call) {
        if (!call.user->roles.admin->roles.vault.canAdd())
            return invalid("You do not have permission to create vault roles");

        validatePositionals(call, resolveUsage({"role", "vault", "create"}));

        if (call.positionals.empty())
            return invalid("Missing required role name");

        const auto& roleName = call.positionals[0];
        const std::string description = call.positionals.size() > 1
            ? call.positionals[1]
            : "";

        if (const auto existing = resolveVaultRole(roleName, "role vault create"); existing.ptr)
            return invalid("Vault role already exists: '" + roleName + "'");

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<vh::rbac::role::Vault>::type;

        auto staged = std::make_shared<vh::rbac::role::Vault>();
        staged->name = roleName;
        staged->description = description;

        const auto exported = staged->toPermissions();
        const auto byFlag = VaultPermissionResolver::buildFlagMap(exported);

        std::vector<std::string> errors;

        for (const auto& opt : call.options) {
            if (!opt.value) continue;

            const auto it = byFlag.find(*opt.value);
            if (it == byFlag.end()) {
                errors.push_back("Unknown permission flag '" + *opt.value + "'");
                continue;
            }

            if (!VaultPermissionResolver::apply(*staged, *it->second.permission, it->second.operation))
                errors.push_back("Failed to apply permission flag '" + *opt.value + "'");
        }

        if (!errors.empty()) {
            std::ostringstream oss;
            oss << "Failed to create vault role from flags:\n";
            for (const auto& e : errors)
                oss << "  - " << e << '\n';
            return invalid(oss.str());
        }

        db::query::rbac::role::Vault::upsert(staged);
        return ok("Role '" + staged->name + "' created successfully");
    }

    static CommandResult handle_update(const CommandCall& call) {
        if (!call.user->roles.admin->roles.vault.canEdit())
            return invalid("You do not have permission to edit vault roles");

        validatePositionals(call, resolveUsage({"role", "vault", "update"}));

        const auto roleLkp = resolveVaultRole(call.positionals[0], "role vault update");
        if (!roleLkp.ptr) return invalid(roleLkp.error);

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<vh::rbac::role::Vault>::type;

        auto staged = std::make_shared<vh::rbac::role::Vault>(*roleLkp.ptr);
        const auto exported = staged->toPermissions();
        const auto byFlag = VaultPermissionResolver::buildFlagMap(exported);

        std::vector<std::string> errors;

        for (const auto& opt : call.options) {
            if (!opt.value) continue;

            const auto it = byFlag.find(*opt.value);
            if (it == byFlag.end()) {
                errors.push_back("Unknown permission flag '" + *opt.value + "'");
                continue;
            }

            if (!VaultPermissionResolver::apply(*staged, *it->second.permission, it->second.operation))
                errors.push_back("Failed to apply permission flag '" + *opt.value + "'");
        }

        if (!errors.empty()) {
            std::ostringstream oss;
            oss << "Failed to update permissions from flags:\n";
            for (const auto& e : errors)
                oss << "  - " << e << '\n';
            return invalid(oss.str());
        }

        db::query::rbac::role::Vault::upsert(staged);
        return ok("Role '" + staged->name + "' updated successfully");
    }

    static CommandResult handle_delete(const CommandCall& call) {
        if (!call.user->roles.admin->roles.vault.canDelete())
            return invalid("You do not have permission to delete vault roles");

        validatePositionals(call, resolveUsage({"role", "vault", "delete"}));

        const auto roleLkp = resolveVaultRole(call.positionals[0], "role vault delete");
        if (!roleLkp.ptr) return invalid(roleLkp.error);

        if (db::query::rbac::role::vault::Assignments::countAssignmentsForRole(roleLkp.ptr->id) > 0)
            return invalid("Cannot delete role '" + roleLkp.ptr->name + "' because it has active assignments. Remove those assignments before deleting the role.");

        db::query::rbac::role::Vault::remove(roleLkp.ptr->id);

        return ok("Role '" + roleLkp.ptr->name + "' deleted successfully");
    }

    static CommandResult handle_info(const CommandCall& call) {
        if (!call.user->roles.admin->roles.vault.canView())
            return invalid("You do not have permission to view vault roles");

        validatePositionals(call, resolveUsage({"role", "vault", "info"}));

        if (const auto roleLkp = resolveVaultRole(call.positionals[0], "role vault list");
            roleLkp && roleLkp.ptr) return ok(to_string(*roleLkp.ptr));
        else if (roleLkp) return invalid(roleLkp.error);

        return invalid("Role not found: '" + call.positionals[0] + "'");
    }

    static CommandResult handle_list(const CommandCall& call) {
        if (!call.user->roles.admin->roles.vault.canView())
            return invalid("You do not have permission to view vault roles");

        validatePositionals(call, resolveUsage({"role", "vault", "list"}));
        return ok(to_string(db::query::rbac::role::Vault::list(parseListQuery(call))));
    }

    static bool is_role_match(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"role", cmd}, input);
    }

    CommandResult handle_vault_roles(const CommandCall &call) {
        const auto usageManager = runtime::Deps::get().shellUsageManager;
        if (call.positionals.empty()) return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (is_role_match("create", sub)) return handle_create(subcall);
        if (is_role_match("update", sub)) return handle_update(subcall);
        if (is_role_match("delete", sub)) return handle_delete(subcall);
        if (is_role_match("info", sub)) return handle_info(subcall);
        if (is_role_match("list", sub)) return handle_list(subcall);

        return invalid(call.constructFullArgs(), "Unknown vault roles subcommand: '" + std::string(sub) + "'");
    }
}
