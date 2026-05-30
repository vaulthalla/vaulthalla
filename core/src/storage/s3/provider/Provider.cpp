#include "storage/s3/provider/Provider.hpp"

namespace vh::storage::s3::provider {
namespace {

bool operationAcceptsStorageClass(const RequestOperation operation) {
    switch (operation) {
    case RequestOperation::PutObject:
    case RequestOperation::CreateMultipartUpload:
    case RequestOperation::CopyObjectRewrite:
        return true;
    case RequestOperation::UploadPart:
    case RequestOperation::CompleteMultipartUpload:
    case RequestOperation::HeadObject:
    case RequestOperation::GetObject:
    case RequestOperation::DeleteObject:
    case RequestOperation::ListObjects:
        return false;
    }
    return false;
}

} // namespace

RequestMutation storageClassMutation(
    const RequestOperation operation,
    const std::optional<StorageTier>& vaultTier) {
    RequestMutation mutation;
    if (!operationAcceptsStorageClass(operation) || !vaultTier || !vaultTier->wire_class) return mutation;
    mutation.system_headers["x-amz-storage-class"] = *vaultTier->wire_class;
    return mutation;
}

} // namespace vh::storage::s3::provider
