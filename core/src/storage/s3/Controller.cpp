#include "storage/s3/Controller.hpp"
#include "storage/ScopedS3RequestUsageCapture.hpp"
#include "vault/model/APIKey.hpp"
#include "storage/s3/curl/helpers.hpp"
#include "log/Registry.hpp"

#include <algorithm>
#include <ctime>
#include <curl/curl.h>
#include <sstream>
#include <utility>
#include <vector>

using namespace vh::vault::model;
using namespace vh::storage::s3::curl;

namespace vh::storage::s3 {
    namespace {
        thread_local std::vector<ScopedS3RequestUsageCapture*> requestUsageCaptures;

        ScopedS3RequestUsageCapture* activeRequestUsageCapture() {
            return requestUsageCaptures.empty() ? nullptr : requestUsageCaptures.back();
        }

        bool isNoSuchKeyDeleteResponse(const HttpResponse& resp) {
            if (resp.curl != CURLE_OK || resp.http != 404) return false;
            return resp.body.find("NoSuchKey") != std::string::npos ||
                   resp.body.find("<Code>NotFound</Code>") != std::string::npos;
        }
    }

    Controller::Controller(const std::shared_ptr<APIKey>& apiKey, std::string bucket)
    : apiKey_(apiKey), bucket_(std::move(bucket)) {
        if (!apiKey_) throw std::runtime_error("S3Provider requires a valid S3APIKey");
        ensureCurlGlobalInit();
    }

    Controller::~Controller() = default;

    void Controller::setRequestBudget(const S3RequestBudget& budget) const {
        std::scoped_lock lock(metricsMutex_);
        requestBudget_ = budget;
    }

    void Controller::clearRequestBudget() const {
        std::scoped_lock lock(metricsMutex_);
        requestBudget_.reset();
    }

    void Controller::resetRequestMetrics() const {
        std::scoped_lock lock(metricsMutex_);
        metrics_ = {};
    }

    S3RequestMetrics Controller::requestMetrics() const {
        std::scoped_lock lock(metricsMutex_);
        return metrics_;
    }

    void Controller::pushRequestUsageCapture(ScopedS3RequestUsageCapture* capture) {
        if (capture) requestUsageCaptures.push_back(capture);
    }

    void Controller::popRequestUsageCapture(ScopedS3RequestUsageCapture* capture) noexcept {
        if (!capture || requestUsageCaptures.empty()) return;
        if (requestUsageCaptures.back() == capture) {
            requestUsageCaptures.pop_back();
            return;
        }
        const auto it = std::find(requestUsageCaptures.rbegin(), requestUsageCaptures.rend(), capture);
        if (it != requestUsageCaptures.rend())
            requestUsageCaptures.erase(std::next(it).base());
    }

    void Controller::recordRequest(const RequestKind kind, const uint64_t amount) const {
        if (auto* capture = activeRequestUsageCapture()) {
            switch (kind) {
            case RequestKind::List:
                capture->checkList(amount);
                break;
            case RequestKind::Head:
                capture->checkHead(amount);
                break;
            case RequestKind::Get:
                capture->checkGet(amount);
                break;
            case RequestKind::Put:
                capture->checkPut(amount);
                break;
            case RequestKind::Copy:
                capture->checkCopy(amount);
                break;
            case RequestKind::Delete:
                capture->checkDelete(amount);
                break;
            case RequestKind::DownloadBytes:
                capture->checkDownloadBytes(amount);
                break;
            }
        }

        std::scoped_lock lock(metricsMutex_);

        auto bump = [&](uint64_t& current, const std::optional<uint64_t>& limit, const char* label) {
            if (limit && current + amount > *limit) {
                metrics_.budget_exceeded = true;
                metrics_.budget_exceeded_reason = fmt::format("S3 request budget exceeded for {}", label);
                throw RequestBudgetExceeded(metrics_.budget_exceeded_reason, label);
            }
            current += amount;
        };

        switch (kind) {
        case RequestKind::List:
            bump(metrics_.list_requests, requestBudget_ ? requestBudget_->max_list_requests : std::optional<uint64_t>{}, "LIST");
            break;
        case RequestKind::Head:
            bump(metrics_.head_requests, requestBudget_ ? requestBudget_->max_head_requests : std::optional<uint64_t>{}, "HEAD");
            break;
        case RequestKind::Get:
            bump(metrics_.get_requests, requestBudget_ ? requestBudget_->max_get_requests : std::optional<uint64_t>{}, "GET");
            break;
        case RequestKind::Put:
            bump(metrics_.put_requests, requestBudget_ ? requestBudget_->max_put_requests : std::optional<uint64_t>{}, "PUT");
            break;
        case RequestKind::Copy:
            bump(metrics_.copy_requests, requestBudget_ ? requestBudget_->max_copy_requests : std::optional<uint64_t>{}, "COPY");
            break;
        case RequestKind::Delete:
            bump(metrics_.delete_requests, requestBudget_ ? requestBudget_->max_delete_requests : std::optional<uint64_t>{}, "DELETE");
            break;
        case RequestKind::DownloadBytes:
            bump(metrics_.downloaded_bytes, requestBudget_ ? requestBudget_->max_downloaded_bytes : std::optional<uint64_t>{}, "downloaded bytes");
            break;
        }

        if (auto* capture = activeRequestUsageCapture()) {
            switch (kind) {
            case RequestKind::List:
                capture->recordList(amount);
                break;
            case RequestKind::Head:
                capture->recordHead(amount);
                break;
            case RequestKind::Get:
                capture->recordGet(amount);
                break;
            case RequestKind::Put:
                capture->recordPut(amount);
                break;
            case RequestKind::Copy:
                capture->recordCopy(amount);
                break;
            case RequestKind::Delete:
                capture->recordDelete(amount);
                break;
            case RequestKind::DownloadBytes:
                capture->recordDownloadBytes(amount);
                break;
            }
        }
    }

    void Controller::recordUploadBytes(const uint64_t amount) const {
        if (amount == 0) return;
        {
            std::scoped_lock lock(metricsMutex_);
            metrics_.uploaded_bytes += amount;
        }
        if (auto* capture = activeRequestUsageCapture())
            capture->recordUploadBytes(amount);
    }

    void Controller::deleteObject(const fs::path& key) const {
        recordRequest(RequestKind::Delete);

        const CurlEasy tmpHandle;
        const auto [canonical, url] = constructPaths(static_cast<CURL*>(tmpHandle), key);

        const std::string payloadHash = sha256Hex("");
        const SList hdrs = makeSigHeaders("DELETE", canonical, payloadHash);

        HttpResponse resp = performCurl([&](CURL* h){
            curl_easy_setopt(h, CURLOPT_URL, url.c_str());
            curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, "DELETE");
            curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs.get());
        });

        if (isNoSuchKeyDeleteResponse(resp)) {
            log::Registry::cloud()->warn(
                "[S3Provider] deleteObject confirmed object is already absent: key={} HTTP={} Response:\n{}",
                key.string(),
                resp.http,
                resp.body);
            throw ObjectNotFound(fmt::format("Object not found in upstream S3 during delete: {}", key.string()));
        }

        if (!resp.ok()) log::Registry::cloud()->error(
            "[S3Provider] deleteObject failed: CURL={} HTTP={} Response:\n{}",
            resp.curl, resp.http, resp.body
        );

        if (!resp.ok()) throw std::runtime_error(
            fmt::format("Failed to delete object from S3 (HTTP {}): {}", resp.http, resp.body));
    }

    std::u8string Controller::listObjects(const fs::path& prefix) const {
        std::u8string fullXmlResponse;
        std::string continuationToken;
        bool moreResults = true;

        while (moreResults) {
            recordRequest(RequestKind::List);

            CURL* curl = curl_easy_init();
            if (!curl) break;

            std::string escapedPrefix;
            if (!prefix.empty()) escapedPrefix = escapeKeyPreserveSlashes(curl, prefix);

            std::ostringstream uri;
            uri << "/" << bucket_ << "?list-type=2";
            if (!escapedPrefix.empty()) uri << "&prefix=" << escapedPrefix;
            if (!continuationToken.empty()) {
                char* escapedToken = curl_easy_escape(curl, continuationToken.c_str(), static_cast<int>(continuationToken.size()));
                if (!escapedToken) {
                    curl_easy_cleanup(curl);
                    break;
                }
                uri << "&continuation-token=" << escapedToken;
                curl_free(escapedToken);
            }

            const std::string uriStr = uri.str();
            const std::string url = apiKey_->endpoint + uriStr;

            const std::string payloadHash = "UNSIGNED-PAYLOAD";
            const auto hdrMap = buildHeaderMap(payloadHash);
            const std::string authHeader =  buildAuthorizationHeader(apiKey_, "GET", uriStr, hdrMap, payloadHash);

            HeaderList headers;
            headers.add("Authorization: " + authHeader);
            for (const auto& [k, v] : hdrMap) headers.add(k + ": " + v);

            std::string response;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.list);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            const CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                log::Registry::cloud()->error("[S3Provider] listObjects failed: CURL={} Response:\n{}",
                                            res, response);
                break;
            }

            // Append raw XML as UTF-8
            fullXmlResponse += std::u8string(reinterpret_cast<const char8_t*>(response.data()), response.size());

            parsePagination(response, continuationToken, moreResults);
        }

        return fullXmlResponse;
    }

    std::pair<std::string, std::string> Controller::constructPaths(CURL* curl, const fs::path& p, const std::string& query) const {
        const auto escapedKey = escapeKeyPreserveSlashes(curl, p);
        const auto canonicalPath = "/" + bucket_ + "/" + escapedKey + query;
        const auto url = apiKey_->endpoint + canonicalPath;
        return {canonicalPath, url};
    }
}
