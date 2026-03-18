#pragma once

#include "rbac/fs/policy/Decision.hpp"
#include "rbac/fs/policy/Request.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

namespace vh::rbac::fs::policy {

    struct Evaluator {
        [[nodiscard]]
        static Decision evaluate(const permission::vault::Filesystem& perms, const Request& req);

    private:
        [[nodiscard]]
        static std::string resolvePath(const Request& req);

        [[nodiscard]]
        static bool isDirectory(const Request& req);

        [[nodiscard]]
        static bool isValidForFile(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool isValidForDirectory(permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool allowedByBase(const permission::vault::Filesystem& perms, bool isDir,
                                  permission::vault::FilesystemAction action);

        [[nodiscard]]
        static bool allowedByOverride(const permission::Override& o, bool isDir,
                                      permission::vault::FilesystemAction action);

        [[nodiscard]]
        static const permission::Override* findBestOverride(
            const std::vector<permission::Override>& overrides,
            std::string_view absolutePath
        );

        [[nodiscard]]
        static std::size_t scorePattern(const ::vh::rbac::fs::glob::model::Pattern& pattern);
    };

}
