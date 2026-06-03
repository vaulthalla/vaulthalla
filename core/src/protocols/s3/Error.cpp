#include "protocols/s3/Error.hpp"

#include <utility>

namespace vh::protocols::s3 {

S3Error::S3Error(std::string code_, std::string message, const http::status status_, std::string resource_)
    : std::runtime_error(message),
      code(std::move(code_)),
      status(status_),
      resource(std::move(resource_)) {}

S3Error accessDenied(std::string resource) {
    return {"AccessDenied", "Access denied", http::status::forbidden, std::move(resource)};
}

S3Error noSuchBucket(std::string bucket) {
    return {"NoSuchBucket", "The specified bucket does not exist", http::status::not_found, std::move(bucket)};
}

S3Error noSuchKey(std::string key) {
    return {"NoSuchKey", "The specified key does not exist", http::status::not_found, std::move(key)};
}

S3Error noSuchUpload(std::string uploadId) {
    return {"NoSuchUpload", "The specified multipart upload does not exist", http::status::not_found, std::move(uploadId)};
}

S3Error invalidArgument(std::string message, std::string resource) {
    return {"InvalidArgument", std::move(message), http::status::bad_request, std::move(resource)};
}

S3Error invalidRange(std::string resource) {
    return {"InvalidRange", "The requested range is not satisfiable", http::status::range_not_satisfiable, std::move(resource)};
}

S3Error preconditionFailed(std::string resource) {
    return {"PreconditionFailed", "At least one of the preconditions you specified did not hold",
            http::status::precondition_failed, std::move(resource)};
}

S3Error notImplemented(std::string message, std::string resource) {
    return {"NotImplemented", std::move(message), http::status::not_implemented, std::move(resource)};
}

} // namespace vh::protocols::s3
