#include "rbac/s3/policy/Decision.hpp"

#include <sstream>

namespace vh::rbac::s3::policy {

std::string reasonToString(const Decision::Reason reason) {
    switch (reason) {
    case Decision::Reason::Allowed: return "allowed";
    case Decision::Reason::MissingPrincipal: return "missing_principal";
    case Decision::Reason::MissingVault: return "missing_vault";
    case Decision::Reason::NoFilesystemMapping: return "no_filesystem_mapping";
    case Decision::Reason::PrincipalRbacDenied: return "principal_rbac_denied";
    case Decision::Reason::CredentialRoleMissing: return "credential_role_missing";
    case Decision::Reason::CredentialRoleDenied: return "credential_role_denied";
    }
    return "unknown";
}

std::string actionToString(const S3Action action) {
    switch (action) {
    case S3Action::ListBuckets: return "ListBuckets";
    case S3Action::HeadBucket: return "HeadBucket";
    case S3Action::CreateBucket: return "CreateBucket";
    case S3Action::DeleteBucket: return "DeleteBucket";
    case S3Action::ListObjects: return "ListObjects";
    case S3Action::HeadObject: return "HeadObject";
    case S3Action::GetObject: return "GetObject";
    case S3Action::PutObject: return "PutObject";
    case S3Action::DeleteObject: return "DeleteObject";
    case S3Action::DeleteObjects: return "DeleteObjects";
    case S3Action::CopyObjectSource: return "CopyObjectSource";
    case S3Action::CopyObjectDestination: return "CopyObjectDestination";
    case S3Action::CreateMultipartUpload: return "CreateMultipartUpload";
    case S3Action::UploadPart: return "UploadPart";
    case S3Action::CompleteMultipartUpload: return "CompleteMultipartUpload";
    case S3Action::AbortMultipartUpload: return "AbortMultipartUpload";
    case S3Action::ListMultipartUploads: return "ListMultipartUploads";
    case S3Action::ListParts: return "ListParts";
    case S3Action::ManageBucketBinding: return "ManageBucketBinding";
    case S3Action::ManageCredential: return "ManageCredential";
    case S3Action::ManageBudget: return "ManageBudget";
    }
    return "Unknown";
}

std::string Decision::toString() const {
    std::ostringstream out;
    out << "S3 policy decision: " << reasonToString(reason)
        << ", principal_allowed=" << (principal_allowed ? "true" : "false")
        << ", credential_allowed=" << (credential_allowed ? "true" : "false");
    if (credential_decision)
        out << ", credential_fs_reason=" << fs::policy::reasonToString(credential_decision->reason);
    return out.str();
}

} // namespace vh::rbac::s3::policy
