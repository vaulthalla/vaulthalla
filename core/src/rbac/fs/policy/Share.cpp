#include "rbac/fs/policy/Share.hpp"

#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/fs/policy/Evaluator.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/permission/Permission.hpp"
#include "rbac/permission/Selector.hpp"
#include "rbac/role/Vault.hpp"
#include "share/Grant.hpp"
#include "share/Principal.hpp"
#include "share/Scope.hpp"
#include "share/TargetResolver.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace vh::rbac::fs::policy {
namespace {
using Action = permission::vault::FilesystemAction;
using DirectoryPermission = permission::vault::fs::DirectoryPermissions;
using FilePermission = permission::vault::fs::FilePermissions;

[[nodiscard]] const std::vector<permission::Permission>& canonicalVaultPermissions() {
    static const auto permissions = role::Vault::Full().toPermissions();
    return permissions;
}

[[nodiscard]] std::vector<std::string> patternsFor(const vh::share::Grant& grant) {
    const auto root = vh::share::Scope::normalizeVaultPath(grant.root_path);
    std::vector<std::string> patterns{root};

    if (grant.target_type == vh::share::TargetType::Directory)
        patterns.push_back(root == "/" ? std::string{"/**"} : root + "/**");

    return patterns;
}

void addOverrides(
    std::vector<permission::Override>& out,
    const std::vector<std::string>& patterns,
    const permission::Permission& permission
) {
    for (const auto& pattern : patterns) {
        permission::Override override;
        override.permission = permission;
        override.effect = permission::OverrideOpt::ALLOW;
        override.enabled = true;
        override.pattern = fs::glob::model::Pattern::make(pattern);
        out.push_back(std::move(override));
    }
}

void addFileOverrides(
    std::vector<permission::Override>& out,
    const std::vector<std::string>& patterns,
    const FilePermission filePermission
) {
    addOverrides(out, patterns, permission::requirePermissionForEnumValue(
        canonicalVaultPermissions(),
        filePermission
    ));
}

void addDirectoryOverrides(
    std::vector<permission::Override>& out,
    const std::vector<std::string>& patterns,
    const DirectoryPermission directoryPermission
) {
    addOverrides(out, patterns, permission::requirePermissionForEnumValue(
        canonicalVaultPermissions(),
        directoryPermission
    ));
}

[[nodiscard]] bool supportsOperation(const vh::share::Operation operation) {
    return operation != vh::share::Operation::Overwrite;
}

[[nodiscard]] bool isValidSharePrincipal(const vh::share::Principal& principal) {
    return !principal.share_id.empty() &&
           !principal.share_session_id.empty() &&
           principal.vault_id != 0 &&
           principal.root_entry_id != 0;
}

void addOverridesForAction(
    std::vector<permission::Override>& overrides,
    const std::vector<std::string>& patterns,
    const Action action,
    const vh::share::TargetType targetType
) {
    if (const auto filePermission = Evaluator::filePermissionForAction(action))
        addFileOverrides(overrides, patterns, *filePermission);

    if (targetType == vh::share::TargetType::Directory) {
        if (const auto directoryPermission = Evaluator::directoryPermissionForAction(action))
            addDirectoryOverrides(overrides, patterns, *directoryPermission);
    }
}

[[nodiscard]] role::Vault emptyScopedVaultRole(
    const std::string& shareId,
    const vh::share::Grant& grant
) {
    auto role = role::Vault::ImplicitDeny();
    role.name = shareId.empty() ? "share_scoped_effective" : "share_link_" + shareId;
    role.description = "Scoped vault role for link share " + (shareId.empty() ? std::string{"runtime"} : shareId);
    role.assignment = role::Vault::AssignmentInfo{
        .subject_id = grant.root_entry_id,
        .vault_id = grant.vault_id,
        .subject_type = "share"
    };
    return role;
}

constexpr std::array shareOperations{
    vh::share::Operation::Metadata,
    vh::share::Operation::List,
    vh::share::Operation::Preview,
    vh::share::Operation::Download,
    vh::share::Operation::Upload,
    vh::share::Operation::Mkdir,
    vh::share::Operation::Overwrite
};
}

role::Vault Share::effectiveVaultRole(
    const vh::share::Principal& principal,
    const vh::share::Operation operation
) {
    auto role = emptyScopedVaultRole(principal.share_id, principal.grant);

    if (!principal.allows(operation) || !supportsOperation(operation)) return role;

    const auto patterns = patternsFor(principal.grant);
    auto& overrides = role.fs.overrides;

    if (const auto action = actionForOperation(operation))
        addOverridesForAction(overrides, patterns, *action, principal.grant.target_type);

    return role;
}

role::Vault Share::effectiveVaultRole(const vh::share::Principal& principal) {
    return scopedVaultRoleForGrant(principal.share_id, principal.grant);
}

role::Vault Share::scopedVaultRoleForGrant(
    const std::string& shareId,
    const vh::share::Grant& grant
) {
    auto role = emptyScopedVaultRole(shareId, grant);
    const auto patterns = patternsFor(grant);

    auto& overrides = role.fs.overrides;
    for (const auto operation : shareOperations) {
        if (!grant.allows(operation) || !supportsOperation(operation)) continue;
        if (const auto action = actionForOperation(operation))
            addOverridesForAction(overrides, patterns, *action, grant.target_type);
    }

    return role;
}

std::optional<permission::vault::FilesystemAction> Share::actionForOperation(
    const vh::share::Operation operation
) {
    switch (operation) {
        case vh::share::Operation::Metadata: return Action::Lookup;
        case vh::share::Operation::List: return Action::List;
        case vh::share::Operation::Preview: return Action::Preview;
        case vh::share::Operation::Download: return Action::Read;
        case vh::share::Operation::Upload: return Action::Write;
        case vh::share::Operation::Mkdir: return Action::Touch;
        case vh::share::Operation::Overwrite:
            return std::nullopt;
    }
    return std::nullopt;
}

Decision Share::evaluate(const vh::share::Principal& principal, const ShareRequest& request) {
    if (!isValidSharePrincipal(principal))
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingUser
        };

    if (!principal.hasVault(request.vault_id))
        return {
            .allowed = false,
            .reason = Decision::Reason::DeniedByBasePermissions,
            .evaluated_path = request.vault_path
        };

    const auto normalizedPath = vh::share::Scope::normalizeVaultPath(request.vault_path.string());
    if (!vh::share::Scope::contains(principal.root_path, normalizedPath))
        return {
            .allowed = false,
            .reason = Decision::Reason::DeniedByBasePermissions,
            .evaluated_path = normalizedPath
        };

    const auto action = actionForOperation(request.operation);
    if (!action || !principal.allows(request.operation))
        return {
            .allowed = false,
            .reason = Decision::Reason::DeniedByBasePermissions,
            .evaluated_path = normalizedPath
        };

    const auto role = principal.scoped_vault_role
                          ? *principal.scoped_vault_role
                          : effectiveVaultRole(principal, request.operation);
    return Evaluator::evaluate(role.fs, {
        .action = *action,
        .vaultPath = normalizedPath,
        .exists = request.target_exists,
        .isDirectory = request.target_type == vh::share::TargetType::Directory
    });
}

Decision Share::evaluate(const Actor& actor, const ShareRequest& request) {
    if (!actor.isShare() || actor.canUseHumanPrivileges() || !actor.sharePrincipal())
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingUser,
            .evaluated_path = request.vault_path
        };

    return evaluate(*actor.sharePrincipal(), request);
}

Decision Share::evaluate(
    const vh::share::Principal& principal,
    const vh::share::ResolvedTarget& target
) {
    return evaluate(principal, {
        .vault_id = target.vault_id,
        .vault_path = target.vault_path,
        .operation = target.operation,
        .target_type = target.target_type,
        .target_exists = !!target.entry
    });
}

Decision Share::evaluate(const Actor& actor, const vh::share::ResolvedTarget& target) {
    if (!actor.isShare() || !actor.sharePrincipal())
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingUser,
            .evaluated_path = target.vault_path
        };

    return evaluate(*actor.sharePrincipal(), target);
}

}
