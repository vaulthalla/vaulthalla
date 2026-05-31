#pragma once

#include "db/query/s3/Gateway.hpp"

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vh::protocols::s3::xml {

struct Bucket {
    std::string name;
    std::time_t created_at{};
};

struct DeletedObject {
    std::string key;
};

struct DeleteError {
    std::string key;
    std::string code;
    std::string message;
};

std::string escape(std::string_view value);
std::string error(const std::string& code, const std::string& message,
                  const std::string& resource, const std::string& requestId);
std::string listBuckets(const std::vector<Bucket>& buckets, const std::string& ownerName = "vaulthalla");
std::string listObjectsV2(const std::string& bucketName, const db::query::s3::ObjectListResult& result,
                          const std::string& prefix, const std::optional<std::string>& delimiter,
                          uint32_t maxKeys, const std::optional<std::string>& encodingType = std::nullopt);
std::string deleteResult(const std::vector<DeletedObject>& deleted,
                         const std::vector<DeleteError>& errors,
                         bool quiet = false);
std::string initiateMultipartUpload(const std::string& bucket, const std::string& key, const std::string& uploadId);
std::string completeMultipartUpload(const std::string& location, const std::string& bucket,
                                    const std::string& key, const std::string& etag);
std::string listMultipartUploads(const std::string& bucket, const std::vector<db::query::s3::MultipartUpload>& uploads);
std::string listParts(const std::string& bucket, const std::string& key, const std::string& uploadId,
                      const std::vector<db::query::s3::MultipartPart>& parts);

std::string httpDate(std::time_t ts);
std::string iso8601(std::time_t ts);

}
