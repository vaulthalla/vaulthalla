#include "email/Transport.hpp"

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>

namespace vh::email {

namespace {

void ensureCurlGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t writeToString(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

}

HttpResponse CurlTransport::send(const HttpRequest& request) {
    ensureCurlGlobalInit();

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("failed to initialize curl");

    HttpResponse response;
    curl_slist* headers = nullptr;

    for (const auto& header : request.headers)
        headers = curl_slist_append(headers, header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request.timeout.count()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

    if (!request.method.empty() && request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    const auto code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        response.status = 0;
        response.body = curl_easy_strerror(code);
    } else {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        response.status = static_cast<int>(status);
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

}
