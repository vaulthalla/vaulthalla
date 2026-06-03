#pragma once

#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/s3/policy/Decision.hpp"
#include "rbac/s3/policy/Request.hpp"

#include <optional>

namespace vh::rbac::s3::policy {

struct Evaluator {
    [[nodiscard]] static Decision evaluate(const S3PolicyRequest& request);

    [[nodiscard]] static std::optional<permission::vault::FilesystemAction> filesystemActionFor(
        S3Action action,
        bool objectExists,
        bool isDirectoryMarker);
};

} // namespace vh::rbac::s3::policy
