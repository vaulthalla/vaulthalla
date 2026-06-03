#pragma once

#include "protocols/s3/Router.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace vh::protocols::s3 {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    struct Metrics {
        uint64_t activeSessions = 0;
        uint64_t totalRequests = 0;
        uint64_t failedRequests = 0;
    };

    explicit Session(tcp::socket socket);

    void run();
    void cancel() noexcept;

    static void cancelAllActive() noexcept;
    static Metrics metrics() noexcept;

private:
    bool readOne();
    bool handleBufferedRequest(http::request_parser<http::buffer_body>& parser);
    bool handleStreamedRequest(http::request_parser<http::buffer_body>& parser);
    bool writeResponse(Router::Response&& response);
    bool writeReadError(const http::request_header<>& header, http::status status, const std::string& code,
                        const std::string& message);
    void close() noexcept;

    static Router::Request makeRequestFromParser(
        const http::request_parser<http::buffer_body>& parser,
        std::string body = {});

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    Router router_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> nativeHandle_{-1};
    std::mutex socketMutex_;
};

} // namespace vh::protocols::s3
