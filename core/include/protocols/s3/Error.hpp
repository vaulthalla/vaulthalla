#pragma once

#include <boost/beast/http/status.hpp>
#include <stdexcept>
#include <string>

namespace vh::protocols::s3 {

namespace http = boost::beast::http;

class S3Error final : public std::runtime_error {
public:
    S3Error(std::string code, std::string message, http::status status, std::string resource = {});

    std::string code;
    http::status status;
    std::string resource;
};

S3Error accessDenied(std::string resource = {});
S3Error noSuchBucket(std::string bucket);
S3Error noSuchKey(std::string key);
S3Error noSuchUpload(std::string uploadId);
S3Error invalidArgument(std::string message, std::string resource = {});
S3Error invalidRange(std::string resource = {});
S3Error preconditionFailed(std::string resource = {});
S3Error notImplemented(std::string message, std::string resource = {});

}
