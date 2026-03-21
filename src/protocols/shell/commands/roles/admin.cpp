#include "protocols/shell/commands/roles.hpp"
#include "protocols/shell/types.hpp"

#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/resolver/admin/all.hpp"

#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/admin/Assignments.hpp"

#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "CommandUsage.hpp"
#include "usages.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "rbac/resolver/Admin.hpp"

using namespace vh::rbac;

namespace vh::protocols::shell::commands::roles {
    static CommandResult handle_create(const CommandCall& call) {
        auto& io = call.io;

        const auto perms = call.user->roles.admin->toPermissions();

        return ok("fixme");
    }

    static CommandResult handle_update(const CommandCall& call) {
        if (!call.user->roles.admin->roles.admin.canEdit()) return invalid("You do not have permission to edit admin roles");
        validatePositionals(call, resolveUsage({"role", "admin", "update"}));
        return ok("fixme");
    }

    static CommandResult handle_delete(const CommandCall& call) {
        if (!call.user->roles.admin->roles.admin.canDelete()) return invalid("You do not have permission to delete admin roles");
        validatePositionals(call, resolveUsage({"role", "admin", "delete"}));

        const auto roleLkp = resolveAdminRole(call.positionals[0], "role admin delete");
        if (!roleLkp.ptr) return invalid(roleLkp.error);

        if (db::query::rbac::role::admin::Assignments::countAssignmentsForRole(roleLkp.ptr->id) > 0)
            return invalid("Cannot delete role '" + roleLkp.ptr->name + "' because it has active assignments. Remove those assignments before deleting the role.");

        db::query::rbac::role::Admin::remove(roleLkp.ptr->id);

        return ok("Role '" + roleLkp.ptr->name + "' deleted successfully");
    }

    static CommandResult handle_info(const CommandCall& call) {
        if (!call.user->roles.admin->roles.admin.canView()) return invalid("You do not have permission to view admin roles");
        validatePositionals(call, resolveUsage({"role", "admin", "info"}));

        if (const auto roleLkp = resolveAdminRole(call.positionals[0], "role admin list");
            roleLkp && roleLkp.ptr) return ok(to_string(*roleLkp.ptr));
        else if (roleLkp) return invalid(roleLkp.error);

        return invalid("Role not found: '" + call.positionals[0] + "'");
    }

    static CommandResult handle_list(const CommandCall& call) {
        if (!call.user->roles.admin->roles.admin.canView()) return invalid("You do not have permission to view admin roles");
        validatePositionals(call, resolveUsage({"role", "admin", "list"}));
        return ok(to_string(db::query::rbac::role::Admin::list(parseListQuery(call))));
    }

    static bool is_role_match(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"role", cmd}, input);
    }

    CommandResult handle_admin_roles(const CommandCall &call) {
        const auto usageManager = runtime::Deps::get().shellUsageManager;
        if (call.positionals.empty()) return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (is_role_match("create", sub)) return handle_create(subcall);
        if (is_role_match("update", sub)) return handle_update(subcall);
        if (is_role_match("delete", sub)) return handle_delete(subcall);
        if (is_role_match("info", sub)) return handle_info(subcall);
        if (is_role_match("list", sub)) return handle_list(subcall);

        return invalid(call.constructFullArgs(), "Unknown admin roles subcommand: '" + std::string(sub) + "'");
    }
}
