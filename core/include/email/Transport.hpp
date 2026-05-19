#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace vh::email {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    std::chrono::seconds timeout = std::chrono::seconds(15);
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string headers;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

class CurlTransport final : public Transport {
public:
    HttpResponse send(const HttpRequest& request) override;
};

class FakeTransport final : public Transport {
public:
    HttpResponse response;
    std::vector<HttpRequest> requests;

    HttpResponse send(const HttpRequest& request) override {
        requests.push_back(request);
        return response;
    }
};

}
