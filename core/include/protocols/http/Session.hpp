#pragma once

#include "protocols/http/Router.hpp"
#include "protocols/SessionLifetimes.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace vh::protocols::http {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket);

    // A session for an accepted socket, registered so cancelAllActive() reaches it even before a pool thread runs it.
    [[nodiscard]] static std::shared_ptr<Session> open(tcp::socket socket);

    void run();
    void cancel() noexcept;

    static void cancelAllActive() noexcept;
    // True once every session object (and so every socket) is gone; false if the timeout passed first.
    [[nodiscard]] static bool waitUntilNoneAlive(std::chrono::milliseconds timeout);

private:
    bool read_one();
    bool handle_buffered_request(boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser);
    bool handle_streaming_upload(boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser);
    bool write_response(model::preview::Response&& response);
    void do_close();

    static request make_request_from_parser(
        const boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser,
        std::string body = {}
    );

    SessionLifetimes::Token lifetime_;   // first: released after socket_ is destroyed
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> nativeHandle_{-1};
    std::mutex socketMutex_;
};

}
