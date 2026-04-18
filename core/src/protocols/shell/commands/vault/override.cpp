#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "db/query/rbac/permission/Override.hpp"
#include "db/query/rbac/role/Vault.hpp"

#include "vault/model/Vault.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/resolver/permission/all.hpp"
#include "rbac/resolver/vault/all.hpp"

#include "config/Registry.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <boost/beast/http/field.hpp>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::identities;
using namespace vh::rbac;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::config;

namespace vh::protocols::shell::commands::vault {
    static CommandResult handle_vault_role_override_add(const CommandCall& call) {
        constexpr const auto* ERR = "vault role override add";

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

        const auto patt = parseGlobPatternOpt(call, true, ERR);
        if (!patt.ok) return invalid(patt.error);

        const auto en = parseEnableDisableOpt(call, ERR);
        if (!en.ok) return invalid(en.error);
        const bool enabled = en.value.value_or(true);

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::AssignOverride,
            .target_subject_type = subj.type,
            .target_subject_id = subj.id,
            .vault_id = vault->id
        })) return invalid("You do not have permission to assign overrides for this vault and subject");

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<vh::rbac::role::Vault>>::type;

        auto staged = std::make_shared<vh::rbac::role::Vault>(*roleLkp.ptr);
        const auto exported = staged->toPermissions();
        const auto byFlag = VaultPermissionResolver::buildFlagMap(exported);

        std::vector<std::string> errors;

        bool foundAtLeastOne = false;

        for (const auto& opt : call.options) {
            if (!opt.value) continue;

            const auto it = byFlag.find(*opt.value);
            if (it == byFlag.end()) {
                errors.emplace_back("Unknown permission flag '" + *opt.value + "'");
                continue;
            }

            if (VaultPermissionResolver::applyOverride(staged, it->second, *patt.pattern, enabled)) foundAtLeastOne = true;
            else errors.emplace_back("Failed to apply permission flag '" + *opt.value + "'");
        }

        if (!foundAtLeastOne) errors.emplace_back("You must specify at least one permission using --<perm> (allow) or --unset-<perm> (deny)");

        if (!errors.empty()) {
            std::string errMsg = ERR + std::string(":") + "\n";
            for (const auto& e : errors) errMsg += "  - " + e + "\n";
            return invalid(errMsg);
        }

        db::query::rbac::role::Vault::upsert(staged);
        roleLkp.ptr->fs.overrides = staged->fs.overrides;

        return ok("Successfully added permission override(s) to role '" + role->name + "' on vault '" + vault->name + "'");
    }

    static CommandResult handle_vault_role_override_update(const CommandCall& call) {
        constexpr const char* ERR = "vault role override update";

        const auto usage = resolveUsage({"vault", "role", "override", "update"});
        validatePositionals(call, usage);

        const auto vaultArg = call.positionals.at(0);
        const auto roleArg  = call.positionals.at(1);
        const auto overrideOpt   = call.positionals.at(2);

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

        const auto en = parseEnableDisableOpt(call, ERR);
        if (!en.ok) return invalid(en.error);
        const bool enabled = en.value.value_or(true);

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::AssignOverride,
            .target_subject_type = subj.type,
            .target_subject_id = subj.id,
            .vault_id = vault->id
        })) return invalid("You do not have permission to assign overrides for this vault and subject");

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<vh::rbac::role::Vault>>::type;

        auto staged = std::make_shared<vh::rbac::role::Vault>(*roleLkp.ptr);
        const auto exported = staged->toPermissions();
        const auto byFlag = VaultPermissionResolver::buildFlagMap(exported);

        permission::Override override;

        std::vector<std::string> errors;

        bool foundAtLeastOne = false;

        for (const auto& opt : call.options) {
            if (!opt.value) continue;

            const auto it = byFlag.find(*opt.value);
            if (it == byFlag.end()) {
                errors.emplace_back("Unknown permission flag '" + *opt.value + "'");
                continue;
            }

            if (VaultPermissionResolver::updateOverride(role, it->second, overrideOpt, enabled)) foundAtLeastOne = true;
            else if (const std::optional<uint32_t> idOpt = parseUInt(overrideOpt)) {
                if (VaultPermissionResolver::updateOverride(role, it->second, *idOpt, enabled)) foundAtLeastOne = true;
                else errors.emplace_back("Failed to apply permission flag '" + *opt.value + "' with override identifier '" + overrideOpt + "'");
            }
        }

        if (!foundAtLeastOne) errors.emplace_back("You must specify at least one permission using --<perm> (allow) or --unset-<perm> (deny)");
        if (!errors.empty()) {
            std::ostringstream oss;
            for (const auto& error : errors) oss << error << std::endl;
            return invalid(oss.str());
        }

        db::query::rbac::role::Vault::upsert(staged);
        roleLkp.ptr->fs.overrides = staged->fs.overrides;

        return ok("Successfully updated permission override(s) for role '" + role->name + "' on vault '" + vault->name + "'");
    }

    static CommandResult handle_vault_role_override_remove(const CommandCall& call) {
        constexpr const char* ERR = "vault override remove";

        const auto usage = resolveUsage({"vault", "role", "override", "remove"});
        validatePositionals(call, usage);

        const auto vaultArg = call.positionals.at(0);
        const auto roleArg  = call.positionals.at(1);
        const auto overrideArg = call.positionals.at(2);

        const auto overrideId = parseUInt(overrideArg);
        if (!overrideId) return invalid("Invalid override identifier: '" + overrideArg + "'. Must be a positive integer.");

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

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::RevokeOverride,
            .target_subject_type = subj.type,
            .target_subject_id = subj.id,
            .vault_id = vault->id
        })) return invalid("You do not have permission to assign overrides for this vault and subject");

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<vh::rbac::role::Vault>>::type;

        auto staged = std::make_shared<vh::rbac::role::Vault>(*role);

        if (VaultPermissionResolver::removeOverride(role, *overrideId)) db::query::rbac::permission::Override::remove(*overrideId);
        else return invalid("No override with identifier '" + overrideArg + "' found for role '" + role->name + "'");

        return ok("Successfully removed override with identifier '" + overrideArg + "' from role '" + role->name + "' on vault '" + vault->name + "'");
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

        if (!resolver::Vault::has<permission::vault::RolePermissions>({
            .user = call.user,
            .permission = permission::vault::RolePermissions::ViewOverride,
            .target_subject_type = subj.type,
            .target_subject_id = subj.id,
            .vault_id = vault->id
        })) return invalid("You do not have permission to view overrides for this vault and subject");

        using VaultPermissionResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<vh::rbac::role::Vault>>::type;

        return ok(to_string(VaultPermissionResolver::overrides(role)));
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
