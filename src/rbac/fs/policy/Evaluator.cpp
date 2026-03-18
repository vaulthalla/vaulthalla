#include "rbac/fs/policy/Evaluator.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/fs/glob/Matcher.hpp"

using namespace vh::rbac::fs::policy;

[[nodiscard]] bool Evaluator::isValidForFile(const permission::vault::FilesystemAction action) {
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

[[nodiscard]] bool Evaluator::isValidForDirectory(const permission::vault::FilesystemAction action) {
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

[[nodiscard]] bool Evaluator::allowedByBase(
    const permission::vault::Filesystem& perms,
    const bool isDir,
    const permission::vault::FilesystemAction action
) {
    using A = permission::vault::FilesystemAction;

    if (isDir) {
        const auto& d = perms.directories;
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

    const auto& f = perms.files;
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

const vh::rbac::permission::Override* Evaluator::findBestOverride(
    const std::vector<permission::Override>& overrides,
    const std::string_view absolutePath
) {
    const permission::Override* best = nullptr;
    std::size_t bestScore = 0;

    for (const auto& o : overrides) {
        if (!o.enabled)
            continue;

        if (!glob::Matcher::matches(o.pattern, std::string{absolutePath}))
            continue;

        const auto score = scorePattern(o.pattern);
        if (!best || score > bestScore) {
            best = &o;
            bestScore = score;
        }
    }

    return best;
}

std::size_t Evaluator::scorePattern(const glob::model::Pattern& pattern) {
    using T = glob::model::Token::Type;

    std::size_t score = 0;
    for (const auto& token : pattern.tokens) {
        switch (token.type) {
            case T::Literal: score += 10 + token.value.size(); break;
            case T::Slash: score += 1; break;
            case T::Question: score += 2; break;
            case T::Star: break;
            case T::DoubleStar: break;
        }
    }
    return score;
}
