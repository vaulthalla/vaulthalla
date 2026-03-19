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

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "auth/Manager.hpp"

using namespace vh::rbac::fs::policy;

namespace {
    using Action = vh::rbac::permission::vault::FilesystemAction;
    using OverrideOpt = vh::rbac::permission::OverrideOpt;

    std::optional<vh::rbac::permission::vault::fs::DirectoryPermissions> tryParseDirectoryPerm(
        const vh::rbac::permission::vault::FilesystemAction &action
    ) {
        using A = vh::rbac::permission::vault::FilesystemAction;
        using D = vh::rbac::permission::vault::fs::DirectoryPermissions;

        switch (action) {
            case A::List: return D::List;
            case A::Upload: return D::Upload;
            case A::Download: return D::Download;
            case A::Rename: return D::Rename;
            case A::Delete: return D::Delete;
            case A::Move: return D::Move;
            case A::Copy: return D::Copy;
            case A::Touch: return D::Touch;
            case A::Preview:
            case A::Overwrite:
            case A::ShareInternal:
            case A::SharePublic:
            case A::SharePublicValidated:
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<vh::rbac::permission::vault::fs::FilePermissions> tryParseFilePerm(
        const vh::rbac::permission::vault::FilesystemAction &action
    ) {
        using A = vh::rbac::permission::vault::FilesystemAction;
        using F = vh::rbac::permission::vault::fs::FilePermissions;

        switch (action) {
            case A::Preview: return F::Preview;
            case A::Upload: return F::Upload;
            case A::Download: return F::Download;
            case A::Overwrite: return F::Overwrite;
            case A::Rename: return F::Rename;
            case A::Delete: return F::Delete;
            case A::Move: return F::Move;
            case A::Copy: return F::Copy;
            case A::ShareInternal:
            case A::SharePublic:
            case A::SharePublicValidated:
            case A::List:
            case A::Touch:
                return std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<vh::rbac::permission::vault::fs::SharePermissions> tryParseSharePerm(
        const vh::rbac::permission::vault::FilesystemAction &action
    ) {
        using A = vh::rbac::permission::vault::FilesystemAction;
        using S = vh::rbac::permission::vault::fs::SharePermissions;

        switch (action) {
            case A::ShareInternal: return S::Internal;
            case A::SharePublic: return S::Public;
            case A::SharePublicValidated: return S::PublicWithValidation;
            case A::Preview:
            case A::Upload:
            case A::Download:
            case A::Overwrite:
            case A::Rename:
            case A::Delete:
            case A::Move:
            case A::Copy:
            case A::List:
            case A::Touch:
                return std::nullopt;
        }

        return std::nullopt;
    }
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

    const auto engine = runtime::Deps::get().storageManager->resolveStorageEngine(
        std::filesystem::path{target.absolutePath}
    );

    if (!engine)
        return {
            .allowed = false,
            .reason = Decision::Reason::StorageEngineNotFound,
            .evaluated_path = target.absolutePath
        };

    const auto vaultId = engine->vault->id;

    // 1) Local/direct vault role: base policy + local overrides
    if (user->roles.vaults.contains(vaultId)) {
        if (const auto role = user->roles.vaults.at(vaultId)) {
            const auto stage = resolveStage(role->fs, target, req.action);
            if (stage.matched && stage.allowed.has_value())
                return {
                    .allowed = *stage.allowed,
                    .reason = stage.reason,
                    .evaluated_path = target.absolutePath,
                    .matched_override = stage.matchedOverride,
                    .override_effect = stage.overrideEffect
                };
        }
    }

    // 2) Group vault roles: overrides only
    for (const auto &group : user->groups) {
        if (!group) continue;
        if (!group->roles.vaults.contains(vaultId)) continue;

        const auto role = group->roles.vaults.at(vaultId);
        if (!role) continue;

        const auto stage = resolveOverrides(
            role->fs.overrides,
            role->fs,
            target.isDir,
            req.action,
            target.absolutePath
        );

        if (stage.matched && stage.allowed.has_value())
            return {
                .allowed = *stage.allowed,
                .reason = stage.reason,
                .evaluated_path = target.absolutePath,
                .matched_override = stage.matchedOverride,
                .override_effect = stage.overrideEffect
            };
    }

    // 3) Global vault filesystem policy: last resort
    if (user->roles.admin) {
        auto& global = user->roles.admin->vGlobals.self;
        if (user->id != engine->vault->owner_id) {
            const auto owner = runtime::Deps::get().authManager->getUser(engine->vault->owner_id);
            if (!owner) throw std::runtime_error("User not found with id: " + std::to_string(engine->vault->owner_id));
            if (owner->isAdmin()) global = user->roles.admin->vGlobals.admin;
            else global = user->roles.admin->vGlobals.user;
        }

        if (allowedByBase(global.fs, target.isDir, req.action))
            return {
                .allowed = true,
                .reason = Decision::Reason::AllowedByBasePermissions,
                .evaluated_path = target.absolutePath
            };
    }

    // 4) Default deny
    return {
        .allowed = false,
        .reason = Decision::Reason::DeniedByBasePermissions,
        .evaluated_path = target.absolutePath
    };
}

std::optional<Decision> Evaluator::resolveTarget(const Request &req, TargetContext &out) {
    if (!req.hasEntry() && !req.hasPath())
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::MissingPathAndEntry
        };

    if ((req.action == Action::Touch || req.action == Action::Upload) && !req.hasPath())
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::MissingPath
        };

    out.absolutePath = resolvePath(req);

    const auto pathObj = std::filesystem::path{out.absolutePath};

    out.entry = req.hasEntry()
        ? req.entry
        : runtime::Deps::get().fsCache->getEntry(pathObj);

    out.exists = !!out.entry;

    out.isDir = out.entry
        ? out.entry->isDirectory()
        : inferIsDirectoryForMissingEntry(req.action);

    if (!out.exists && requiresExistingEntry(req.action))
        return Decision{
            .allowed = false,
            .reason = Decision::Reason::MissingEntry,
            .evaluated_path = out.absolutePath
        };

    if (out.isDir) {
        if (!isValidForDirectory(req.action))
            return Decision{
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = out.absolutePath
            };
    } else {
        if (!isValidForFile(req.action))
            return Decision{
                .allowed = false,
                .reason = Decision::Reason::InvalidActionForEntryType,
                .evaluated_path = out.absolutePath
            };
    }

    return std::nullopt;
}

Evaluator::StageResult Evaluator::resolveStage(
    const permission::vault::Filesystem &perms,
    const TargetContext &target,
    const permission::vault::FilesystemAction action
) {
    if (const auto overrides = resolveOverrides(perms.overrides, perms, target.isDir, action, target.absolutePath);
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
    const permission::vault::Filesystem &perms,
    const bool isDir,
    const permission::vault::FilesystemAction action,
    const std::string_view absolutePath
) {
    if (const auto *best = findBestOverride(overrides, absolutePath)) {
        if (const auto effect = overrideEffect(*best, isDir, perms, action); effect.has_value()) {
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

std::string Evaluator::resolvePath(const Request &req) {
    const auto path = req.hasEntry() ? req.entry->path : *req.path;

    auto normalized = path.lexically_normal().generic_string();
    if (normalized.empty()) return "/";

    return normalized;
}

bool Evaluator::requiresExistingEntry(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Preview:
        case A::Download:
        case A::Overwrite:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
        case A::List:
            return true;

        case A::Touch:
        case A::Upload:
            return false;
    }

    return true;
}

bool Evaluator::inferIsDirectoryForMissingEntry(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Touch:
            return true;

        case A::Upload:
        default:
            return false;
    }
}

bool Evaluator::isValidForFile(const permission::vault::FilesystemAction action) {
    using A = permission::vault::FilesystemAction;

    switch (action) {
        case A::Preview:
        case A::Upload:
        case A::Download:
        case A::Overwrite:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
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
        case A::Upload:
        case A::Download:
        case A::Touch:
        case A::Rename:
        case A::Delete:
        case A::Move:
        case A::Copy:
        case A::ShareInternal:
        case A::SharePublic:
        case A::SharePublicValidated:
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
            case A::Upload: return d.canUpload();
            case A::Download: return d.canDownload();
            case A::Touch: return d.canTouch();
            case A::Rename: return d.canRename();
            case A::Delete: return d.canDelete();
            case A::Move: return d.canMove();
            case A::Copy: return d.canCopy();
            case A::ShareInternal: return d.canShareInternally();
            case A::SharePublic: return d.canSharePublicly();
            case A::SharePublicValidated: return d.canSharePubliclyWithVal();
            case A::Preview:
            case A::Overwrite:
                return false;
        }
    }

    const auto &f = perms.files;

    switch (action) {
        case A::Preview: return f.canPreview();
        case A::Upload: return f.canUpload();
        case A::Download: return f.canDownload();
        case A::Overwrite: return f.canOverwrite();
        case A::Rename: return f.canRename();
        case A::Delete: return f.canDelete();
        case A::Move: return f.canMove();
        case A::Copy: return f.canCopy();
        case A::ShareInternal: return f.canShareInternally();
        case A::SharePublic: return f.canSharePublicly();
        case A::SharePublicValidated: return f.canSharePubliclyWithVal();
        case A::List:
        case A::Touch:
            return false;
    }

    return false;
}

std::optional<vh::rbac::permission::OverrideOpt> Evaluator::overrideEffect(
    const permission::Override &o,
    const bool isDir,
    const permission::vault::Filesystem &perms,
    const permission::vault::FilesystemAction action
) {
    if (!o.enabled) return std::nullopt;

    if (isDir) {
        if (const auto d = tryParseDirectoryPerm(action)) {
            if (!perms.directories.has(*d)) return std::nullopt;
            return o.effect;
        }

        if (const auto s = tryParseSharePerm(action)) {
            if (!perms.directories.share.has(*s)) return std::nullopt;
            return o.effect;
        }

        return std::nullopt;
    }

    if (const auto f = tryParseFilePerm(action)) {
        if (!perms.files.has(*f)) return std::nullopt;
        return o.effect;
    }

    if (const auto s = tryParseSharePerm(action)) {
        if (!perms.files.share.has(*s)) return std::nullopt;
        return o.effect;
    }

    return std::nullopt;
}

const vh::rbac::permission::Override *Evaluator::findBestOverride(
    const std::vector<permission::Override> &overrides,
    const std::string_view absolutePath
) {
    const permission::Override *best = nullptr;
    std::size_t bestScore = 0;

    const auto path = std::filesystem::path{std::string(absolutePath)};

    for (const auto &o : overrides) {
        if (!o.enabled)
            continue;

        if (!glob::Matcher::matches(o.pattern, path))
            continue;

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

    for (const auto &token : pattern.tokens) {
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
