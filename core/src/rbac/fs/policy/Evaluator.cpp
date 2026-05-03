#include "rbac/fs/policy/Evaluator.hpp"

#include "fs/cache/Registry.hpp"
#include "fs/model/Entry.hpp"
#include "identities/Group.hpp"
#include "identities/User.hpp"
#include "rbac/fs/glob/Matcher.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "vault/model/Vault.hpp"
#include "fs/model/Path.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "auth/Manager.hpp"

using namespace vh::rbac::fs::policy;

namespace {
    using Action = vh::rbac::permission::vault::FilesystemAction;
    using OverrideOpt = vh::rbac::permission::OverrideOpt;
}

Decision Evaluator::evaluate(const Request &req) {
    const auto user = req.user;
    if (!user)
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingUser
        };

    TargetContext target;
    if (const auto invalid = resolveTarget(req, target))
        return *invalid;

    const auto vaultId = target.engine->vault->id;

    // 1) Local/direct vault role: base policy + local overrides
    if (user->roles.vaults.contains(vaultId)) {
        if (const auto role = user->roles.vaults.at(vaultId)) {
            const auto stage = resolveStage(role->fs, target, req.action);
            if (stage.matched && stage.allowed.has_value()) {
                if (!*stage.allowed && req.action == permission::vault::FilesystemAction::Lookup
                    && requiresTraversalThrough(role->fs.overrides, target.vaultPath.string()))
                    return {
                        .allowed = true,
                        .reason = Decision::Reason::LowRiskOpRequiredForOverrideTraversal,
                        .evaluated_path = target.vaultPath
                    };

                return {
                    .allowed = *stage.allowed,
                    .reason = stage.reason,
                    .evaluated_path = target.vaultPath,
                    .matched_override = stage.matchedOverride,
                    .override_effect = stage.overrideEffect
                };
            }
        }
    }

    // 2) Group vault roles
    for (const auto &group: user->groups) {
        if (!group) continue;
        if (!group->roles.vaults.contains(vaultId)) continue;

        const auto role = group->roles.vaults.at(vaultId);
        if (!role) continue;

        const auto stage = resolveStage(role->fs, target, req.action);
        if (stage.matched && stage.allowed.has_value()) {
            if (!*stage.allowed && req.action == permission::vault::FilesystemAction::Lookup
                && requiresTraversalThrough(role->fs.overrides, target.vaultPath.string()))
                return {
                    .allowed = true,
                    .reason = Decision::Reason::LowRiskOpRequiredForOverrideTraversal,
                    .evaluated_path = target.vaultPath
                };

            return {
                .allowed = *stage.allowed,
                .reason = stage.reason,
                .evaluated_path = target.vaultPath,
                .matched_override = stage.matchedOverride,
                .override_effect = stage.overrideEffect
            };
        }
    }

    // 3) Global vault filesystem policy: last resort
    std::array vGlobals{
        user->roles.admin->vGlobals.self,
        user->roles.admin->vGlobals.admin,
        user->roles.admin->vGlobals.user
    };
    for (const auto &global: vGlobals) {
        const auto stage = resolveStage(global.fs, target, req.action);
        if (stage.matched && stage.allowed.has_value())
            return {
                .allowed = *stage.allowed,
                .reason = stage.reason,
                .evaluated_path = target.vaultPath,
                .matched_override = stage.matchedOverride,
                .override_effect = stage.overrideEffect
            };
    }

    // 4) Default deny
    return {
        .allowed = false,
        .reason = Decision::Reason::DeniedByBasePermissions,
        .evaluated_path = target.vaultPath
    };
}

std::optional<vh::rbac::permission::vault::fs::DirectoryPermissions> Evaluator::directoryPermissionForAction(
    const permission::vault::FilesystemAction action
) {
    using A = permission::vault::FilesystemAction;
    using D = permission::vault::fs::DirectoryPermissions;

    switch (action) {
        case A::List: return D::List;
        case A::Write: return D::Upload;
        case A::Read: return D::Download;
        case A::Rename: return D::Rename;
        case A::Delete: return D::Delete;
        case A::Move: return D::Move;
        case A::Copy: return D::Copy;
        case A::Touch: return D::Touch;
        case A::Lookup: return D::List;
        case A::Preview:
        case A::Overwrite:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
            return std::nullopt;
    }

    return std::nullopt;
}

std::optional<vh::rbac::permission::vault::fs::FilePermissions> Evaluator::filePermissionForAction(
    const permission::vault::FilesystemAction action
) {
    using A = permission::vault::FilesystemAction;
    using F = permission::vault::fs::FilePermissions;

    switch (action) {
        case A::Preview: return F::Preview;
        case A::Write: return F::Upload;
        case A::Read: return F::Download;
        case A::Overwrite: return F::Overwrite;
        case A::Rename: return F::Rename;
        case A::Delete: return F::Delete;
        case A::Move: return F::Move;
        case A::Copy: return F::Copy;
        case A::Lookup: return F::Preview;
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
        case A::List:
        case A::Touch:
            return std::nullopt;
    }

    return std::nullopt;
}

Decision Evaluator::evaluate(
    const permission::vault::Filesystem &perms,
    const ResolvedRequest &req
) {
    if (req.vaultPath.empty())
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingPath
        };

    if (!req.exists && requiresExistingEntry(req.action))
        return {
            .allowed = false,
            .reason = Decision::Reason::MissingEntry,
            .evaluated_path = req.vaultPath
        };

    if (req.isDirectory) {
        if (!isValidForDirectory(req.action))
            return {
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = req.vaultPath
            };
    } else {
        if (!isValidForFile(req.action))
            return {
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = req.vaultPath
            };
    }

    TargetContext target;
    target.vaultPath = req.vaultPath;
    target.exists = req.exists;
    target.isDir = req.isDirectory;

    const auto stage = resolveStage(perms, target, req.action);
    if (stage.matched && stage.allowed.has_value()) {
        if (!*stage.allowed && req.action == permission::vault::FilesystemAction::Lookup
            && requiresTraversalThrough(perms.overrides, target.vaultPath.string()))
            return {
                .allowed = true,
                .reason = Decision::Reason::LowRiskOpRequiredForOverrideTraversal,
                .evaluated_path = target.vaultPath
            };

        return {
            .allowed = *stage.allowed,
            .reason = stage.reason,
            .evaluated_path = target.vaultPath,
            .matched_override = stage.matchedOverride,
            .override_effect = stage.overrideEffect
        };
    }

    return {
        .allowed = false,
        .reason = Decision::Reason::DeniedByBasePermissions,
        .evaluated_path = target.vaultPath
    };
}

[[nodiscard]]
static bool hasTargetIdentity(const Request &req) noexcept {
    return req.hasEntry() || req.hasPath();
}

std::optional<Decision> Evaluator::resolveTarget(const Request &req, TargetContext &out) {
    if (!hasTargetIdentity(req))
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::MissingPathAndEntry
        };

    if (req.hasPath()) out.fusePath = *req.path;
    if (req.hasEntry()) out.vaultPath = req.entry->path;

    if (req.vaultId) out.engine = runtime::Deps::get().storageManager->getEngine(*req.vaultId);

    if (!out.engine && req.hasEntry() && req.entry->vault_id)
        out.engine = runtime::Deps::get().storageManager->getEngine(*req.entry->vault_id);

    if (!out.engine && !out.fusePath.empty())
        out.engine = runtime::Deps::get().storageManager->resolveStorageEngine(std::filesystem::path{out.fusePath});

    if (!out.engine)
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::StorageEngineNotFound,
            .evaluated_path = out.fusePath
        };

    if (out.fusePath.empty())
        out.fusePath = out.engine->paths->absRelToAbsRel(out.vaultPath, vh::fs::model::PathType::VAULT_ROOT,
                                                         vh::fs::model::PathType::FUSE_ROOT);

    if (out.vaultPath.empty())
        out.vaultPath = out.engine->paths->absRelToAbsRel(out.fusePath, vh::fs::model::PathType::FUSE_ROOT,
                                                          vh::fs::model::PathType::VAULT_ROOT);

    // sanity check - paths should resolve by now one way or another for valid requests
    if (out.fusePath.empty() || out.vaultPath.empty())
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::UnableToResolvePaths,
            .evaluated_path = out.fusePath
        };

    out.entry = req.hasEntry()
                    ? req.entry
                    : runtime::Deps::get().fsCache->getEntry(out.fusePath);

    out.exists = !!out.entry;
    out.isDir = out.entry
                    ? out.entry->isDirectory()
                    : inferIsDirectoryForMissingEntry(req.action);

    if (!out.exists && requiresExistingEntry(req.action))
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::MissingEntry,
            .evaluated_path = out.vaultPath
        };

    if (out.isDir) {
        if (!isValidForDirectory(req.action))
            return Decision{
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = out.vaultPath
            };
    } else {
        if (!isValidForFile(req.action))
            return Decision{
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = out.vaultPath
            };
    }

    return std::nullopt;
}

Evaluator::StageResult Evaluator::resolveStage(
    const permission::vault::Filesystem &perms,
    const TargetContext &target,
    const permission::vault::FilesystemAction action
) {
    if (const auto overrides = resolveOverrides(perms.overrides, target.vaultPath.string(), action);
        overrides.matched)
        return overrides;

    const bool allowed = allowedByBase(perms, target.isDir, action);

    return {
        .matched = true,
        .allowed = allowed,
        .reason = allowed
                      ? Decision::Reason::AllowedByBasePermissions
                      : Decision::Reason::DeniedByBasePermissions
    };
}

Evaluator::StageResult Evaluator::resolveOverrides(
    const std::vector<permission::Override> &overrides,
    const std::string_view absolutePath,
    const permission::vault::FilesystemAction& action
) {
    const auto *best = findBestOverride(overrides, absolutePath, action);
    if (best) {
        if (const auto effect = overrideEffect(*best); effect.has_value()) {
            if (*effect == OverrideOpt::ALLOW)
                return {
                    .matched = true,
                    .allowed = true,
                    .reason = Decision::Reason::AllowedByOverride,
                    .matchedOverride = best->pattern.source,
                    .overrideEffect = effect
                };

            if (*effect == OverrideOpt::DENY)
                return {
                    .matched = true,
                    .allowed = false,
                    .reason = Decision::Reason::DeniedByOverride,
                    .matchedOverride = best->pattern.source,
                    .overrideEffect = effect
                };
        }
    }

    return {};
}

bool Evaluator::requiresExistingEntry(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Preview:
        case A::Read:
        case A::Overwrite:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
        case A::List:
        case A::Lookup:
            return true;

        case A::Touch:
        case A::Write:
            return false;
    }

    return true;
}

bool Evaluator::inferIsDirectoryForMissingEntry(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Touch:
            return true;

        case A::Write:
        default:
            return false;
    }
}

bool Evaluator::isValidForFile(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Preview:
        case A::Write:
        case A::Read:
        case A::Overwrite:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
        case A::Lookup:
            return true;

        case A::List:
        case A::Touch:
            return false;
    }

    return false;
}

bool Evaluator::isValidForDirectory(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::List:
        case A::Write:
        case A::Read:
        case A::Touch:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
        case A::Lookup:
            return true;

        case A::Preview:
        case A::Overwrite:
            return false;
    }

    return false;
}

bool Evaluator::allowedByBase(
    const permission::vault::Filesystem &perms,
    const bool isDir,
    const permission::vault::FilesystemAction action
) {
    using A = permission::vault::FilesystemAction;

    if (isDir) {
        const auto &d = perms.directories;

        switch (action) {
            case A::List: return d.canList();
            case A::Write: return d.canUpload();
            case A::Read: return d.canDownload();
            case A::Touch: return d.canTouch();
            case A::Rename: return d.canRename();
            case A::Delete: return d.canDelete();
            case A::Move: return d.canMove();
            case A::Copy: return d.canCopy();
            case A::ShareInternal: return d.canShareInternally();
            case A::SharePublic: return d.canSharePublicly();
            case A::SharePublicValidated: return d.canSharePubliclyWithVal();
            case A::Lookup: return d.canList();
            case A::Preview:
            case A::Overwrite:
                return false;
        }
    }

    const auto &f = perms.files;

    switch (action) {
        case A::Preview: return f.canPreview();
        case A::Write: return f.canUpload();
        case A::Read: return f.canDownload();
        case A::Overwrite: return f.canOverwrite();
        case A::Rename: return f.canRename();
        case A::Delete: return f.canDelete();
        case A::Move: return f.canMove();
        case A::Copy: return f.canCopy();
        case A::ShareInternal: return f.canShareInternally();
        case A::SharePublic: return f.canSharePublicly();
        case A::SharePublicValidated: return f.canSharePubliclyWithVal();
        case A::Lookup: return f.canPreview();
        case A::List:
        case A::Touch:
            return false;
    }

    return false;
}

std::optional<vh::rbac::permission::OverrideOpt> Evaluator::overrideEffect(
    const permission::Override &o
) {
    if (!o.enabled) return std::nullopt;
    return o.effect;
}

bool Evaluator::requiresTraversalThrough(
    const std::vector<permission::Override> &overrides,
    const std::filesystem::path &absolutePath
) {
    return std::ranges::any_of(overrides, [&](const permission::Override &o) {
        return o.enabled &&
               o.effect == permission::OverrideOpt::ALLOW &&
               glob::Matcher::requiresTraversalThrough(o.pattern, absolutePath);
    });
}

const vh::rbac::permission::Override *Evaluator::findBestOverride(
    const std::vector<permission::Override> &overrides,
    const std::filesystem::path &absolutePath,
    const rbac::permission::vault::FilesystemAction &action
) {
    const permission::Override *best = nullptr;
    std::size_t bestScore = 0;

    for (const auto &o : overrides) {
        if (!o.enabled) continue;

        if (const auto fPerm = permission::vault::fs::Files::resolveFromQualifiedName(o.permission.qualified_name); fPerm.has_value()) {
            const auto reqPerm = filePermissionForAction(action);
            if (!reqPerm.has_value() || *fPerm != *reqPerm) continue;
        }

        if (const auto dPerm = permission::vault::fs::Directories::resolveFromQualifiedName(o.permission.qualified_name); dPerm.has_value()) {
            const auto reqPerm = directoryPermissionForAction(action);
            if (!reqPerm.has_value() || *dPerm != *reqPerm) continue;
        }

        if (!glob::Matcher::matches(o.pattern, absolutePath)) continue;

        const auto score = scorePattern(o.pattern);
        if (!best || score > bestScore) {
            best = &o;
            bestScore = score;
        }
    }

    return best;
}

std::size_t Evaluator::scorePattern(const glob::model::Pattern &pattern) {
    using T = glob::model::Token::Type;

    std::size_t score = 0;

    for (const auto &token: pattern.tokens) {
        switch (token.type) {
            case T::Literal:
                score += 10 + token.value.size();
                break;

            case T::Slash:
                score += 1;
                break;

            case T::Question:
                score += 2;
                break;

            case T::Star:
            case T::DoubleStar:
                break;
        }
    }

    return score;
}
