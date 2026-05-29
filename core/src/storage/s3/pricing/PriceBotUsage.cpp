#include "storage/s3/pricing/PriceBotUsage.hpp"

#include "sync/model/Action.hpp"

namespace vh::storage::s3::pricing {
namespace {

void putCount(std::map<std::string, std::string>& out, const std::string& key, const std::uint64_t value) {
    if (value == 0) return;
    out[key] = std::to_string(value);
}

} // namespace

UsageInput toPriceBotUsageInput(
    const vh::sync::model::S3CostEstimate& estimate,
    const std::optional<provider::StorageTier>& tier) {
    UsageInput usage;
    putCount(usage.provider_operation_counts, "ListObjectsV2", estimate.list_requests);
    putCount(usage.provider_operation_counts, "HeadObject", estimate.head_requests);
    putCount(usage.provider_operation_counts, "GetObject", estimate.get_requests);
    putCount(usage.provider_operation_counts, "PutObject", estimate.put_requests);
    putCount(usage.provider_operation_counts, "CopyObject", estimate.copy_requests);
    putCount(usage.provider_operation_counts, "DeleteObject", estimate.delete_requests);
    // TODO: split multipart create/upload-part/complete operations once the
    // planner exposes multipart request pressure separately.

    usage.downloaded_bytes = std::to_string(estimate.planned_body_download_bytes);
    usage.uploaded_bytes = std::to_string(estimate.planned_upload_bytes);
    usage.object_count = std::to_string(estimate.remote_index_objects);

    if (tier && tier->retrieval_fee_possible && estimate.planned_body_download_bytes > 0)
        usage.retrieval_bytes = std::to_string(estimate.planned_body_download_bytes);

    return usage;
}

} // namespace vh::storage::s3::pricing
