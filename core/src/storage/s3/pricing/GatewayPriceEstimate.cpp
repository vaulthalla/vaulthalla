#include "storage/s3/pricing/GatewayPriceEstimate.hpp"

#include "storage/CloudEngine.hpp"
#include "storage/s3/provider/StorageTier.hpp"
#include "sync/model/Action.hpp"

#include <algorithm>
#include <utility>

namespace vh::storage::s3::pricing {
namespace {

uint64_t countOrOne(const uint64_t value) {
    return value == 0 ? 1 : value;
}

vh::sync::model::S3CostEstimate usageFor(const S3GatewayPriceEstimateRequest& request) {
    vh::sync::model::S3CostEstimate estimate;
    const auto requests = countOrOne(request.request_count);
    const auto objects = countOrOne(request.object_count);

    switch (request.operation) {
    case S3GatewayOperation::ListBuckets:
    case S3GatewayOperation::ListObjectsV2:
    case S3GatewayOperation::ListMultipartUploads:
    case S3GatewayOperation::ListParts:
        estimate.list_requests = requests;
        break;
    case S3GatewayOperation::HeadBucket:
    case S3GatewayOperation::HeadObject:
        estimate.head_requests = requests;
        break;
    case S3GatewayOperation::CreateBucket:
    case S3GatewayOperation::PutObject:
    case S3GatewayOperation::CreateMultipartUpload:
    case S3GatewayOperation::UploadPart:
    case S3GatewayOperation::CompleteMultipartUpload:
        estimate.put_requests = requests;
        estimate.planned_upload_bytes = request.upload_bytes;
        break;
    case S3GatewayOperation::GetObject:
        estimate.get_requests = requests;
        estimate.planned_body_download_bytes = request.download_bytes;
        break;
    case S3GatewayOperation::DeleteBucket:
    case S3GatewayOperation::DeleteObject:
    case S3GatewayOperation::AbortMultipartUpload:
        estimate.delete_requests = requests;
        break;
    case S3GatewayOperation::DeleteObjects:
        estimate.delete_requests = std::max<uint64_t>(objects, requests);
        break;
    case S3GatewayOperation::CopyObject:
        estimate.copy_requests = requests;
        estimate.get_requests = requests;
        estimate.put_requests = requests;
        estimate.planned_body_download_bytes = request.download_bytes;
        estimate.planned_upload_bytes = request.upload_bytes;
        break;
    }

    estimate.remote_index_objects = request.object_count;
    return estimate;
}

S3GatewayPriceEstimate fromReport(PriceEstimateReport report) {
    return {
        .available = report.available,
        .supported = report.supported,
        .currency = report.currency,
        .estimated_cost = report.estimated_cost,
        .unavailable_reason = report.unavailable_reason,
        .as_price_estimate_report = std::move(report)
    };
}

} // namespace

std::string toString(const S3GatewayOperation operation) {
    switch (operation) {
    case S3GatewayOperation::ListBuckets: return "ListBuckets";
    case S3GatewayOperation::HeadBucket: return "HeadBucket";
    case S3GatewayOperation::CreateBucket: return "CreateBucket";
    case S3GatewayOperation::DeleteBucket: return "DeleteBucket";
    case S3GatewayOperation::ListObjectsV2: return "ListObjectsV2";
    case S3GatewayOperation::HeadObject: return "HeadObject";
    case S3GatewayOperation::GetObject: return "GetObject";
    case S3GatewayOperation::PutObject: return "PutObject";
    case S3GatewayOperation::DeleteObject: return "DeleteObject";
    case S3GatewayOperation::DeleteObjects: return "DeleteObjects";
    case S3GatewayOperation::CopyObject: return "CopyObject";
    case S3GatewayOperation::CreateMultipartUpload: return "CreateMultipartUpload";
    case S3GatewayOperation::UploadPart: return "UploadPart";
    case S3GatewayOperation::CompleteMultipartUpload: return "CompleteMultipartUpload";
    case S3GatewayOperation::AbortMultipartUpload: return "AbortMultipartUpload";
    case S3GatewayOperation::ListMultipartUploads: return "ListMultipartUploads";
    case S3GatewayOperation::ListParts: return "ListParts";
    }
    return "Unknown";
}

S3GatewayPriceEstimate estimateGatewayS3Request(
    const vh::storage::CloudEngine& engine,
    const S3GatewayPriceEstimateRequest& request,
    PriceEstimateOptions options,
    IPriceBotClient* client,
    IPriceCatalogStore* catalogStore) {
    if (options.mode != PriceEstimateMode::BudgetConservative)
        options.mode = PriceEstimateMode::BudgetConservative;

    if (request.storage_class) {
        const auto profile = engine.s3ProviderProfile();
        if (!profile)
            return fromReport(PriceEstimateReport::unsupported("S3 provider has no price-bot profile"));
        const auto resolution = profile->normalizeStorageTier(*request.storage_class);
        if (!resolution.ok)
            return fromReport(PriceEstimateReport::unsupported(resolution.error));
        options.storage_tier_override = resolution.resolved;
    }

    return fromReport(estimatePlannedS3Sync(engine, usageFor(request), options, client, catalogStore));
}

} // namespace vh::storage::s3::pricing
