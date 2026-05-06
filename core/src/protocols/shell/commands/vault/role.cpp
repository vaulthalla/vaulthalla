#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"

#include "vault/model/Vault.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "identities/Group.hpp"
#include "rbac/resolver/vault/all.hpp"

#include "config/Registry.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <utility>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::identities;
using namespace vh::rbac;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::config;

namespace vh::protocols::shell::commands::vault {
    static CommandResult handle_vault_role_assign(const CommandCall &call) {
        constexpr const auto *ERR = "vault role assign";

        const auto usage = resolveUsage({"vault", "role", "assign"});
        validatePositionals(call, usage);

        const auto vaultArg = call.positionals.at(0);
        const auto roleArg = call.positionals.at(1);

        const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
        if (!vLkp) return invalid(vLkp.error);
        const auto vault = vLkp.ptr;

        const auto roleLkp = resolveVaultRole(roleArg, ERR);
        if (!roleLkp || !roleLkp.ptr) return invalid(roleLkp.error);
        const auto role = roleLkp.ptr;

        const auto subjLkp = parseSubject(call, ERR);
        if (!subjLkp) return invalid(subjLkp.error);
        const auto [subjectType, subjectId] = *subjLkp.ptr;

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::Assign,
            .target_subject_type = subjectType,
            .target_subject_id = subjectId,
            .vault_id = vault->id
        }))
            return invalid("vault role assign: you do not have permission to assign this role to the specified subject");

        db::query::rbac::role::vault::Assignments::assign(vault->id, subjectType, subjectId, role->id);
        const auto vr = db::query::rbac::role::vault::Assignments::get(vault->id, subjectType, subjectId);
        if (!vr) return invalid("vault role assign: failed to verify role assignment after database operation");

        if (subjectType == "user") {
            const auto user = db::query::identities::User::getUserById(subjectId);
            if (!user) return invalid("vault role assign: failed to retrieve user after database operation");
            return ok(
                "Successfully assigned role '" + role->name + "' to user '" + user->name + "' for vault '" + vault->name +
                "'");
        }

        if (subjectType == "group") {
            const auto group = db::query::identities::Group::getGroup(subjectId);
            if (!group) return invalid("vault role assign: failed to retrieve group after database operation");
            return ok(
                "Successfully assigned role '" + role->name + "' to group '" + group->name + "' for vault '" + vault->name +
                "'");
        }

        return invalid("vault role assign: unknown subject type '" + subjectType + "' - expected 'user' or 'group'");
    }

    static CommandResult handle_vault_role_remove(const CommandCall &call) {
        constexpr const auto *ERR = "vault role remove";

        const auto usage = resolveUsage({"vault", "role", "remove"});
        validatePositionals(call, usage);

        const auto vaultArg = call.positionals.at(0);
        const auto roleArg = call.positionals.at(1);

        const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
        if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
        const auto vault = vLkp.ptr;

        const auto subjLkp = parseSubject(call, ERR);
        if (!subjLkp || !subjLkp.ptr) return invalid(subjLkp.error);
        const auto subj = *subjLkp.ptr;

        const auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
        if (!roleLkp || !roleLkp.ptr) return invalid(roleLkp.error);
        const auto role = roleLkp.ptr;

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::Revoke,
            .target_subject_type = subj.type,
            .target_subject_id = subj.id,
            .vault_id = vault->id
        }))
            return invalid("vault role remove: you do not have permission to remove this role from the specified subject");

        db::query::rbac::role::vault::Assignments::unassign(vault->id, subj.type, subj.id);

        return ok("Successfully removed role '" + role->name + "' from vault '" + vault->name + "'");
    }

    static CommandResult handle_vault_role_list(const CommandCall &call) {
        constexpr const auto *ERR = "vault role list";

        const auto usage = resolveUsage({"vault", "role", "list"});
        validatePositionals(call, usage);

        std::shared_ptr<::vh::vault::model::Vault> vault;
        if (!call.positionals.empty()) {
            const auto vaultArg = call.positionals.at(0);

            const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
            if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
            vault = vLkp.ptr;
        }

        std::vector<std::shared_ptr<::vh::rbac::role::Vault>> roles;
        if (vault) {
            if (!resolver::Vault::has<permission::vault::RolePermissions>({
                .user = call.user,
                .permission = permission::vault::RolePermissions::View,
                .vault_id = vault->id
            }))
                return invalid("vault list: you do not have permission to view roles for this vault");
            roles = db::query::rbac::role::vault::Assignments::listForVault(vault->id);
        } else {
            roles = db::query::rbac::role::vault::Assignments::listAll();
            std::erase_if(roles, [&](const std::shared_ptr<::vh::rbac::role::Vault> &r) {
                return !resolver::Vault::has<permission::vault::RolePermissions>({
                    .user = call.user,
                    .permission = permission::vault::RolePermissions::View,
                    .vault_id = r->assignment->vault_id
                });
            });
        }

        if (roles.empty()) return ok("No vault roles found.");

        nlohmann::json j;
        j["roles"] = roles;
        return ok(j.dump(4));
    }

    static bool isVaultRoleMatch(const std::string &cmd, const std::string_view input) {
        return isCommandMatch({"vault", "role", cmd}, input);
    }

    CommandResult handle_vault_role(const CommandCall &call) {
        if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
            return usage(call.constructFullArgs());

        const auto usageManager = runtime::Deps::get().shellUsageManager;
        const auto [sub, subcall] = descend(call);

        if (isVaultRoleMatch({"assign"}, sub)) return handle_vault_role_assign(subcall);
        if (isVaultRoleMatch({"remove"}, sub)) return handle_vault_role_remove(subcall);
        if (isVaultRoleMatch({"list"}, sub)) return handle_vault_role_list(subcall);
        if (isVaultRoleMatch({"override"}, sub)) return handle_vault_role_override(subcall);

        return invalid(call.constructFullArgs(), "Unknown vault role subcommand: '" + std::string(sub) + "'");
    }
}
