#pragma once

#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/fs/policy/Decision.hpp"
#include "rbac/fs/policy/Request.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vh::identities {
    struct User;
    struct Group;
}

namespace vh::storage { struct Engine; }

namespace vh::rbac::fs::policy {
    struct Evaluator {
        [[nodiscard]]
        static Decision evaluate(const Request &req);

    private:
        struct TargetContext {
            std::shared_ptr<storage::Engine> engine{nullptr};
            std::filesystem::path fusePath, vaultPath;
            std::shared_ptr<vh::fs::model::Entry> entry;
            bool exists{false};
            bool isDir{false};
        };

        struct StageResult {
            bool matched{false};
            std::optional<bool> allowed{};
            Decision::Reason reason{Decision::Reason::DeniedByBasePermissions};
            std::optional<std::string> matchedOverride{};
            std::optional<permission::OverrideOpt> overrideEffect{};
        };

        [[nodiscard]]
        static std::optional<Decision> resolveTarget(const Request &req, TargetContext &out);

        [[nodiscard]]
        static StageResult resolveStage(
            const permission::vault::Filesystem &perms,
            const TargetContext &target,
            permission::vault::FilesystemAction action
        );

        [[nodiscard]]
        static StageResult resolveOverrides(
            const std::vector<permission::Override> &overrides,
            std::string_view absolutePath
        );

        [[nodiscard]]
        static bool requiresExistingEntry(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool inferIsDirectoryForMissingEntry(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool isValidForFile(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool isValidForDirectory(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool allowedByBase(
            const permission::vault::Filesystem &perms,
            bool isDir,
            permission::vault::FilesystemAction action
        );

        [[nodiscard]]
        static std::optional<permission::OverrideOpt> overrideEffect(const permission::Override &o);

        [[nodiscard]] static bool requiresTraversalThrough(
            const std::vector<permission::Override> &overrides,
            const std::filesystem::path& absolutePath
        );

        [[nodiscard]] static const permission::Override *findBestOverride(
            const std::vector<permission::Override> &overrides,
            const std::filesystem::path& absolutePath
        );

        [[nodiscard]]
        static std::size_t scorePattern(const ::vh::rbac::fs::glob::model::Pattern &pattern);
    };
}
