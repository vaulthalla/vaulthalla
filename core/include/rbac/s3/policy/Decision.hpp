#pragma once

#include "rbac/fs/policy/Decision.hpp"
#include "rbac/s3/policy/Request.hpp"

#include <optional>
#include <string>

namespace vh::rbac::s3::policy {

struct Decision {
    enum class Reason : uint8_t {
        Allowed,
        MissingPrincipal,
        MissingVault,
        NoFilesystemMapping,
        PrincipalRbacDenied,
        CredentialRoleMissing,
        CredentialRoleDenied
    };

    bool allowed{false};
    Reason reason{Reason::PrincipalRbacDenied};
    bool principal_allowed{false};
    bool credential_allowed{false};
    std::optional<fs::policy::Decision> credential_decision;

    [[nodiscard]] std::string toString() const;
};

std::string reasonToString(Decision::Reason reason);
std::string actionToString(S3Action action);

} // namespace vh::rbac::s3::policy
