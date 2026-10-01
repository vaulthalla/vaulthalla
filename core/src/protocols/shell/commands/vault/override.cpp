#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "protocols/shell/util/permissionFlags.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "ops/Roles.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/role/Vault.hpp"
#include "vault/model/Vault.hpp"

#include <string>

namespace vh::protocols::shell::commands::vault {
    // vault positional + --user/--group subject -> the assignment the overrides belong to.
    static std::optional<CommandResult> overrideCliTarget(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage,
                                                          const char* err, ops::roles::VaultSubject& out,
                                                          std::string& vaultName) {
        const auto vLkp = resolveVault(call, call.positionals.at(0), usage, err);
        if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
        const auto subjLkp = parseSubject(call, err);
        if (!subjLkp || !subjLkp.ptr) return invalid(subjLkp.error);
        out = {.vault_id = vLkp.ptr->id, .subject = {.type = subjLkp.ptr->type, .id = subjLkp.ptr->id}};
        vaultName = vLkp.ptr->name;
        return std::nullopt;
    }

    static std::optional<bool> overrideCliEnabled(const CommandCall& call) {
        if (hasFlag(call, "enable") && hasFlag(call, "disable"))
            throw ops::Invalid("--enable and --disable are mutually exclusive");
        if (hasFlag(call, "disable")) return false;
        if (hasFlag(call, "enable")) return true;
        return std::nullopt;
    }

    static unsigned int overrideCliId(const std::string& arg) {
        const auto id = parseUInt(arg);
        if (!id || *id == 0) throw ops::Invalid("override identifier must be a positive integer, got '" + arg + "'");
        return *id;
    }

    static CommandResult handle_vault_role_override_add(const CommandCall& call) {
        const auto usage = resolveUsage({"vault", "role", "override", "add"});
        validatePositionals(call, usage);

        ops::roles::VaultSubject target;
        std::string vaultName;
        if (auto err = overrideCliTarget(call, usage, "vault role override add", target, vaultName)) return *err;

        return runOp("vault role override add", [&] {
            const auto pattern = optVal(call, "pattern");
            if (!pattern || pattern->empty()) throw ops::Invalid("--pattern is required");
            return ops::roles::addVaultRoleOverrides(call.user, {
                .target = target,
                .permissions = permissionEditFromFlags<vh::rbac::role::Vault>(call, usage),
                .pattern = *pattern,
                .enabled = overrideCliEnabled(call).value_or(true)
            });
        }, [&](const auto& created) {
            return "Successfully added " + std::to_string(created.size()) + " permission override(s) on vault '" +
                   vaultName + "'\n" + to_string(created);
        });
    }

    static CommandResult handle_vault_role_override_update(const CommandCall& call) {
        const auto usage = resolveUsage({"vault", "role", "override", "update"});
        validatePositionals(call, usage);

        ops::roles::VaultSubject target;
        std::string vaultName;
        if (auto err = overrideCliTarget(call, usage, "vault role override update", target, vaultName)) return *err;

        return runOp("vault role override update", [&] {
            if (hasFlag(call, "allow") && hasFlag(call, "deny")) throw ops::Invalid("--allow and --deny are mutually exclusive");
            std::optional<bool> allow;
            if (hasFlag(call, "allow")) allow = true;
            if (hasFlag(call, "deny")) allow = false;
            return ops::roles::updateVaultRoleOverride(call.user, {
                .target = target,
                .override_id = overrideCliId(call.positionals.at(2)),
                .allow = allow,
                .pattern = optVal(call, "pattern"),
                .enabled = overrideCliEnabled(call)
            });
        }, [&](const auto& updated) {
            return "Successfully updated override " + std::to_string(updated.id) + " on vault '" + vaultName + "'\n" +
                   to_string(updated);
        });
    }

    static CommandResult handle_vault_role_override_remove(const CommandCall& call) {
        const auto usage = resolveUsage({"vault", "role", "override", "remove"});
        validatePositionals(call, usage);

        ops::roles::VaultSubject target;
        std::string vaultName;
        if (auto err = overrideCliTarget(call, usage, "vault role override remove", target, vaultName)) return *err;

        return runOp("vault role override remove", [&] {
            ops::roles::removeVaultRoleOverride(call.user, target, overrideCliId(call.positionals.at(2)));
        }, [&] {
            return "Successfully removed override '" + call.positionals.at(2) + "' on vault '" + vaultName + "'";
        });
    }

    static CommandResult handle_vault_role_override_list(const CommandCall& call) {
        const auto usage = resolveUsage({"vault", "role", "override", "list"});
        validatePositionals(call, usage);

        ops::roles::VaultSubject target;
        std::string vaultName;
        if (auto err = overrideCliTarget(call, usage, "vault role override list", target, vaultName)) return *err;

        return runOp("vault role override list", [&] { return ops::roles::listVaultRoleOverrides(call.user, target); },
            [](const auto& overrides) { return to_string(overrides); });
    }

    static bool isVaultRoleOverrideMatch(const std::string& cmd, const std::string_view input) {
        return isCommandMatch({"vault", "role", "override", cmd}, input);
    }

    CommandResult handle_vault_role_override(const CommandCall& call) {
        if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
            return usage(call.constructFullArgs());

        const auto [sub, subcall] = descend(call);

        if (isVaultRoleOverrideMatch({"add"}, sub)) return handle_vault_role_override_add(subcall);
        if (isVaultRoleOverrideMatch({"remove"}, sub)) return handle_vault_role_override_remove(subcall);
        if (isVaultRoleOverrideMatch({"update"}, sub)) return handle_vault_role_override_update(subcall);
        if (isVaultRoleOverrideMatch({"list"}, sub)) return handle_vault_role_override_list(subcall);

        return invalid(call.constructFullArgs(), "Unknown vault override action: '" + std::string(sub) + "'");
    }
}
