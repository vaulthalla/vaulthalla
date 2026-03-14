#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "db/query/rbac/Permission.hpp"

#include "vault/model/Vault.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/Permission.hpp"

#include "config/Registry.hpp"
#include "CommandUsage.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <regex>
#include <utility>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::identities;
using namespace vh::rbac::model;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::config;

static VaultPermission permFromString(const std::string& perm) {
    if (perm.empty()) throw std::invalid_argument("Vault permission string cannot be empty");

    if (perm == "manage-vault")      return VaultPermission::ManageVault;
    if (perm == "manage-access")     return VaultPermission::ManageAccess;
    if (perm == "manage-tags")       return VaultPermission::ManageTags;
    if (perm == "manage-metadata")   return VaultPermission::ManageMetadata;
    if (perm == "manage-versions")   return VaultPermission::ManageVersions;
    if (perm == "manage-file-locks") return VaultPermission::ManageFileLocks;
    if (perm == "share")             return VaultPermission::Share;
    if (perm == "sync")              return VaultPermission::Sync;
    if (perm == "create")            return VaultPermission::Create;
    if (perm == "download")          return VaultPermission::Download;
    if (perm == "delete")            return VaultPermission::Delete;
    if (perm == "rename")            return VaultPermission::Rename;
    if (perm == "move")              return VaultPermission::Move;
    if (perm == "list")              return VaultPermission::List;
    throw std::invalid_argument("Unknown vault permission string: " + perm);
}

struct ParsedPermsResult { std::vector<VaultPermission> allow, deny; };

static ParsedPermsResult parseExplicitVaultPermissionFlags(const CommandCall& call) {
    ParsedPermsResult res;

    const auto parseFlag = [&call, &res](const std::string& flag) {
        if (hasFlag(call, flag) || hasFlag(call, "allow-" + flag)) res.allow.push_back(permFromString(flag));
        if (hasFlag(call, "deny-" + flag)) res.deny.push_back(permFromString(flag));
    };

    parseFlag("manage-vault");
    parseFlag("manage-access");
    parseFlag("manage-tags");
    parseFlag("manage-metadata");
    parseFlag("manage-versions");
    parseFlag("manage-file-locks");
    parseFlag("share");
    parseFlag("sync");
    parseFlag("create");
    parseFlag("download");
    parseFlag("delete");
    parseFlag("rename");
    parseFlag("move");
    parseFlag("list");

    return res;
}

static CommandResult handle_vault_role_override_add(const CommandCall& call) {
    constexpr const char* ERR = "vault role override add";

    const auto usage = resolveUsage({"vault", "role", "override", "add"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg  = call.positionals.at(1);

    auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp) return invalid(vLkp.error);
    auto vault = vLkp.ptr;

    if (auto err = checkOverridePermissions(call, vault, ERR)) return invalid(*err);

    const auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp) return invalid(subjLkp.error);
    const Subject subj = *subjLkp.ptr;

    const auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
    if (!roleLkp) return invalid(roleLkp.error);
    const auto role = roleLkp.ptr;

    const auto parsedPerms = parseExplicitVaultPermissionFlags(call);
    if (parsedPerms.allow.empty() && parsedPerms.deny.empty())
        return invalid(std::string(ERR) + ": must specify at least one permission using --<perm> (allow) or --unset-<perm> (deny)");

    const auto patt = parsePatternOpt(call, /*required=*/true, ERR);
    if (!patt.ok) return invalid(patt.error);

    const auto en = parseEnableDisableOpt(call, ERR);
    if (!en.ok) return invalid(en.error);
    const bool enabled = en.value.value_or(true);

    const auto applyOne = [&](const VaultPermission& perm, OverrideOpt effect) {
        const auto ov = std::make_shared<Override>();
        ov->assignment_id = role->id;
        ov->permission = *db::query::rbac::Permission::getPermissionByName(get_vault_perm_name(perm));
        ov->pattern     = *patt.compiled;
        ov->patternStr  = patt.raw;
        ov->effect      = effect;
        ov->enabled     = enabled;
        db::query::rbac::Permission::addVPermOverride(ov);
    };

    for (const auto& p : parsedPerms.allow) applyOne(p, OverrideOpt::ALLOW);
    for (const auto& p : parsedPerms.deny)  applyOne(p, OverrideOpt::DENY);

    return ok("Successfully added permission override(s) to role '" + role->name + "' on vault '" + vault->name + "'");
}

static CommandResult handle_vault_role_override_update(const CommandCall& call) {
    constexpr const char* ERR = "vault role override update";

    const auto usage = resolveUsage({"vault", "role", "override", "update"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg  = call.positionals.at(1);
    const auto bitArg   = call.positionals.at(2);

    auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp) return invalid(vLkp.error);
    auto vault = vLkp.ptr;

    if (auto err = checkOverridePermissions(call, vault, ERR)) return invalid(*err);

    auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp) return invalid(subjLkp.error);
    const Subject subj = *subjLkp.ptr;

    auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
    if (!roleLkp) return invalid(roleLkp.error);
    auto role = roleLkp.ptr;

    std::string bitErr;
    auto bitPosOpt = parsePositiveUint(bitArg, "bit position", bitErr);
    if (!bitPosOpt) return invalid(std::string(ERR) + ": " + bitErr);
    const unsigned int bitPosition = *bitPosOpt;

    db::query::rbac::VPermOverrideQuery q{
        .vault_id     = vault->id,
        .subject_type = subj.type,
        .subject_id   = subj.id,
        .bit_position = bitPosition
    };

    auto ov = db::query::rbac::Permission::getVPermOverride(q);
    if (!ov) {
        return invalid(std::string(ERR) + ": no override found for (vault="
                       + std::to_string(vault->id) + ", " + subj.type + "="
                       + std::to_string(subj.id) + ", bit=" + std::to_string(bitPosition) + ")");
    }

    // Defensive: ensure the override fetched indeed belongs to the resolved role assignment
    if (ov->assignment_id != role->id) {
        return invalid(std::string(ERR) + ": override does not belong to role '"
                       + role->name + "' (assignment mismatch)");
    }

    bool changed = false;

    const auto eff = parseEffectChangeOpt(call, ERR);
    if (!eff.ok) return invalid(eff.error);
    if (eff.value) {
        ov->effect = *eff.value;
        changed = true;
    }

    const auto patt = parsePatternOpt(call, /*required=*/false, ERR);
    if (!patt.ok) return invalid(patt.error);
    if (patt.compiled) {
        ov->pattern    = *patt.compiled;
        ov->patternStr = patt.raw;
        changed = true;
    }

    const auto en = parseEnableDisableOpt(call, ERR);
    if (!en.ok) return invalid(en.error);
    if (en.value) {
        ov->enabled = *en.value;
        changed = true;
    }

    if (!changed)
        return invalid(std::string(ERR) + ": no changes specified (set at least one of --allow/--deny, --path/--pattern, --enable/--disable)");

    db::query::rbac::Permission::updateVPermOverride(ov);

    return ok("Updated override (vault=" + std::to_string(vault->id) +
              ", " + subj.type + "=" + std::to_string(subj.id) +
              ", bit=" + std::to_string(bitPosition) + ") on role '" + role->name + "'");
}

static CommandResult handle_vault_role_override_remove(const CommandCall& call) {
    constexpr const char* ERR = "vault override remove";

    const auto usage = resolveUsage({"vault", "role", "override", "remove"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg  = call.positionals.at(1);
    const auto bitArg   = call.positionals.at(2);

    auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp) return invalid(vLkp.error);
    auto vault = vLkp.ptr;

    if (auto err = checkOverridePermissions(call, vault, ERR)) return invalid(*err);

    auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp) return invalid(subjLkp.error);
    const Subject subj = *subjLkp.ptr;

    auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
    if (!roleLkp) return invalid(roleLkp.error);
    auto role = roleLkp.ptr;

    std::string bitErr;
    auto bitPosOpt = parsePositiveUint(bitArg, "bit position", bitErr);
    if (!bitPosOpt) return invalid(std::string(ERR) + ": " + bitErr);
    const unsigned int bitPosition = *bitPosOpt;

    db::query::rbac::VPermOverrideQuery q{
        .vault_id     = vault->id,
        .subject_type = subj.type,
        .subject_id   = subj.id,
        .bit_position = bitPosition
    };

    auto ov = db::query::rbac::Permission::getVPermOverride(q);
    if (!ov) {
        return invalid(std::string(ERR) + ": no override found for (vault="
                       + std::to_string(vault->id) + ", " + subj.type + "="
                       + std::to_string(subj.id) + ", bit=" + std::to_string(bitPosition) + ")");
    }
    if (ov->assignment_id != role->id) {
        return invalid(std::string(ERR) + ": override does not belong to role '"
                       + role->name + "' (assignment mismatch)");
    }

    db::query::rbac::Permission::removeVPermOverride(ov->id);

    return ok("Removed override (vault=" + std::to_string(vault->id) +
              ", " + subj.type + "=" + std::to_string(subj.id) +
              ", bit=" + std::to_string(bitPosition) + ") from role '" + role->name + "'");
}

static CommandResult handle_vault_role_override_list(const CommandCall& call) {
    constexpr const auto* ERR = "vault override list";

    const auto usage = resolveUsage({"vault", "role", "override", "list"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg  = call.positionals.at(1);

    const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    if (const auto err = checkOverridePermissions(call, vault, ERR)) return invalid(*err);

    const auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp) return invalid(subjLkp.error);
    const Subject subj = *subjLkp.ptr;

    const auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
    if (!roleLkp) return invalid(roleLkp.error);
    const auto role = roleLkp.ptr;

    const auto& ovs = role->permission_overrides;

    if (ovs.empty()) {
        return ok("No overrides found for role '" + role->name + "' in vault '" + vault->name +
                  "' for " + subj.type + " id " + std::to_string(subj.id));
    }

    std::vector<std::shared_ptr<Override>> overrides = ovs;
    std::ranges::sort(overrides.begin(), overrides.end(),
              [](const auto& a, const auto& b) {
                  return a->permission.bit_position < b->permission.bit_position;
              });

    return ok(to_string(overrides));
}

static CommandResult handle_vault_role_assign(const CommandCall& call) {
    constexpr const auto* ERR = "vault role override update";

    const auto usage = resolveUsage({"vault", "role", "assign"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg = call.positionals.at(1);

    const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    if (vault->owner_id != call.user->id) {
        if (!call.user->canManageVaults() && !call.user->canManageVaultAccess(vault->id)) return invalid(
            "vault assign: you do not have permission to assign roles to this vault");

        if (!call.user->canManageRoles()) return invalid("vault assign: you do not have permission to manage roles");
    }

    const auto roleLkp = resolveRole(roleArg, ERR);
    if (!roleLkp || !roleLkp.ptr) return invalid(roleLkp.error);
    const auto role = roleLkp.ptr;

    const auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp) return invalid(subjLkp.error);
    const auto [subjectType, subjectId] = *subjLkp.ptr;

    const auto vr = std::make_shared<Vault>();
    vr->role_id = role->id;
    vr->vault_id = vault->id;
    vr->subject_type = subjectType;
    vr->subject_id = subjectId;

    db::query::rbac::Permission::assignVaultRole(vr);

    return ok("Successfully assigned role '" + role->name + "' to vault '" + vault->name + "'");
}

static CommandResult handle_vault_role_remove(const CommandCall& call) {
    constexpr const auto* ERR = "vault role remove";

    const auto usage = resolveUsage({"vault", "role", "remove"});
    validatePositionals(call, usage);

    const auto vaultArg = call.positionals.at(0);
    const auto roleArg = call.positionals.at(1);

    const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    if (vault->owner_id != call.user->id) {
        if (!call.user->canManageVaults() && !call.user->canManageVaultAccess(vault->id)) return invalid(
            "vault remove: you do not have permission to remove roles from this vault");

        if (!call.user->canManageRoles()) return invalid("vault remove: you do not have permission to manage roles");
    }

    const auto subjLkp = parseSubject(call, ERR);
    if (!subjLkp || !subjLkp.ptr) return invalid(subjLkp.error);
    const auto subj = *subjLkp.ptr;

    const auto roleLkp = resolveVRole(roleArg, vault, &subj, ERR);
    if (!roleLkp || !roleLkp.ptr) return invalid(roleLkp.error);
    const auto role = roleLkp.ptr;

    db::query::rbac::Permission::removeVaultRoleAssignment(role->id);

    return ok("Successfully removed role '" + role->name + "' from vault '" + vault->name + "'");
}

static CommandResult handle_vault_role_list(const CommandCall& call) {
    constexpr const auto* ERR = "vault role list";

    const auto usage = resolveUsage({"vault", "role", "list"});
    validatePositionals(call, usage);

    std::shared_ptr<Vault> vault;
    if (!call.positionals.empty()) {
        const auto vaultArg = call.positionals.at(0);

        const auto vLkp = resolveVault(call, vaultArg, usage, ERR);
        if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
        vault = vLkp.ptr;

        if (vault->owner_id != call.user->id) {
            if (!call.user->canManageVaults() && !call.user->canManageVaultAccess(vault->id)) return invalid(
                "vault list: you do not have permission to view roles for this vault");
        }
    }

    std::vector<std::shared_ptr<Vault>> roles;
    if (vault) roles = db::query::rbac::Permission::listVaultAssignedRoles(vault->id);
    else {
        if (!call.user->canManageRoles()) return invalid("vault list: you do not have permission to manage roles");
        roles = db::query::rbac::Permission::listVaultAssignedRoles(0); // List all roles
    }

    nlohmann::json j;
    j["roles"] = roles;
    return ok(j.dump(4));
}

static bool isVaultRoleMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"vault", "role", cmd}, input);
}

static bool isVaultRoleOverrideMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"vault", "role", "override", cmd}, input);
}

static CommandResult handle_vault_role_override(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);

    if (isVaultRoleOverrideMatch({"add"}, sub)) return handle_vault_role_override_add(subcall);
    if (isVaultRoleOverrideMatch({"remove"}, sub)) return handle_vault_role_override_remove(subcall);
    if (isVaultRoleOverrideMatch({"update"}, sub)) return handle_vault_role_override_update(subcall);
    if (isVaultRoleOverrideMatch({"list"}, sub)) return handle_vault_role_override_list(subcall);

    return invalid(call.constructFullArgs(), "Unknown vault override action: '" + std::string(sub) + "'");
}

CommandResult commands::vault::handle_vault_role(const CommandCall& call) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    const auto [sub, subcall] = descend(call);

    if (isVaultRoleMatch({"assign"}, sub)) return handle_vault_role_assign(subcall);
    if (isVaultRoleMatch({"remove"}, sub)) return handle_vault_role_remove(subcall);
    if (isVaultRoleMatch({"list"}, sub)) return handle_vault_role_list(subcall);
    if (isVaultRoleMatch({"override"}, sub)) return handle_vault_role_override(subcall);

    return invalid(call.constructFullArgs(), "Unknown vault role subcommand: '" + std::string(sub) + "'");
}
