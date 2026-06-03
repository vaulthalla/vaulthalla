#pragma once

#include "storage/s3/pricing/PriceEstimate.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace vh::storage { class CloudEngine; }

namespace vh::storage::s3::pricing {

enum class S3GatewayOperation {
    ListBuckets,
    HeadBucket,
    CreateBucket,
    DeleteBucket,
    ListObjectsV2,
    HeadObject,
    GetObject,
    PutObject,
    DeleteObject,
    DeleteObjects,
    CopyObject,
    CreateMultipartUpload,
    UploadPart,
    CompleteMultipartUpload,
    AbortMultipartUpload,
    ListMultipartUploads,
    ListParts
};

struct S3GatewayPriceEstimateRequest {
    uint32_t vault_id{};
    std::optional<uint32_t> gateway_credential_id{std::nullopt};
    std::string provider_key;
    bool provider_supported{false};
    S3GatewayOperation operation{S3GatewayOperation::HeadObject};
    uint64_t request_count{1};
    uint64_t upload_bytes{};
    uint64_t download_bytes{};
    uint64_t object_count{1};
    std::optional<std::string> storage_class{std::nullopt};
};

struct S3GatewayPriceEstimate {
    bool available{false};
    bool supported{false};
    std::string currency;
    std::string estimated_cost;
    std::string unavailable_reason;
    PriceEstimateReport as_price_estimate_report;
};

[[nodiscard]] std::string toString(S3GatewayOperation operation);

[[nodiscard]] S3GatewayPriceEstimate estimateGatewayS3Request(
    const vh::storage::CloudEngine& engine,
    const S3GatewayPriceEstimateRequest& request,
    PriceEstimateOptions options = {.mode = PriceEstimateMode::BudgetConservative},
    IPriceBotClient* client = nullptr,
    IPriceCatalogStore* catalogStore = nullptr);

} // namespace vh::storage::s3::pricing
