#include "storage/s3/Controller.hpp"
#include "vault/model/APIKey.hpp"

#include <ctime>
#include <curl/curl.h>
#include <regex>

using namespace vh::storage::s3;

ValidateResult Controller::validateAPICredentials() const {
    if (apiKey_->secret_access_key.empty())
        throw std::runtime_error("S3Provider: API key secret is empty, cannot validate credentials");

    const std::regex re_key("^[A-Za-z0-9/+=]{20,128}$");
    const std::regex re_endpoint(R"(^https?://([A-Za-z0-9.-]+|\d{1,3}(?:\.\d{1,3}){3})(:\d{1,5})?/?$)");

    std::string errors;
    if (!std::regex_match(apiKey_->access_key, re_key))
        errors += "Access key format looks wrong (expect 20-128 alphanumeric chars, slashes, pluses, or equals).\n";
    if (!std::regex_match(apiKey_->secret_access_key, re_key))
        errors += "Secret access key format looks wrong (expect 20-128 alphanumeric chars, slashes, pluses, or equals).\n";
    if (!std::regex_match(apiKey_->endpoint, re_endpoint))
        errors += "Endpoint format looks wrong (expect https://<host>[:port]/).\n";

    if (!errors.empty())
        return {false, errors};

    // --- Live probe: ListBuckets ---
    const auto rstrip = [](std::string s) {
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    };

    const auto serviceUrl = rstrip(apiKey_->endpoint) + "/";

    static const std::string kCanonical = "/";
    static const std::string kUnsigned = "UNSIGNED-PAYLOAD";

    CurlEasy tmpHandle;
    SList hdrs = makeSigHeaders("GET", kCanonical, kUnsigned);
    hdrs.add("Content-Type: application/xml");

    const auto resp = performCurl([&](CURL* h) {
        curl_easy_setopt(h, CURLOPT_URL, serviceUrl.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs.get());
        curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
        // Make sure no upload/body options are set for this handle
        curl_easy_setopt(h, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(h, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(0));
    });

    if (resp.ok()) return {true, "Credentials validated (ListBuckets succeeded)."};

    const std::string& body = resp.body;
    const bool accessDenied = body.find("AccessDenied") != std::string::npos;
    const bool badSig =
        body.find("SignatureDoesNotMatch") != std::string::npos ||
        body.find("InvalidAccessKeyId") != std::string::npos ||
        body.find("AuthFailure") != std::string::npos ||
        body.find("XAmzContentSHA256Mismatch") != std::string::npos;

    if (accessDenied && !badSig) return {true, "Credentials validated (auth OK, ListBuckets denied)."};
    return {false, "Auth probe failed: " + resp.body};
}

bool Controller::isBucketEmpty() const {
    const auto rstrip = [](std::string s){ while(!s.empty() && s.back()=='/') s.pop_back(); return s; };
    const std::string base = rstrip(apiKey_->endpoint);
    const std::string path = "/" + bucket_;          // canonical URI
    const std::string query = "list-type=2&max-keys=1"; // canonical query string (already sorted)

    const std::string url = base + path + "?" + query;

    static const std::string kUnsigned = "UNSIGNED-PAYLOAD";

    CurlEasy tmpHandle;
    // NEW: pass query to the signer so it’s included in the canonical request
    SList hdrs = makeSigHeaders("GET", path, kUnsigned, query);
    hdrs.add("Content-Type: application/xml");

    const auto resp = performCurl([&](CURL* h) {
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs.get());
        curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(h, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(h, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(0));
    });

    if (!resp.ok()) throw std::runtime_error("S3Provider: failed to query bucket: " + resp.body);
    return resp.body.find("<Contents>") == std::string::npos;
}
