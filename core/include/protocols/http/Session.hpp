#pragma once

#include "protocols/http/Router.hpp"
#include "protocols/SessionLifetimes.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace vh::protocols::http {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

// Blocking I/O on the accepted socket with real deadlines: the socket is non-blocking and every read/write waits
// with poll() bounded by the current deadline (SO_RCVTIMEO/SO_SNDTIMEO are ignored by Asio's blocking ops).
// Satisfies Beast's SyncReadStream/SyncWriteStream.
class TimedStream {
public:
    explicit TimedStream(tcp::socket& socket) : socket_(socket) {}

    void setTimeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }

    template<class MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec) {
        return io(true, [&](boost::system::error_code& e) { return socket_.read_some(buffers, e); }, ec);
    }
    template<class MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers) {
        boost::system::error_code ec;
        const auto n = read_some(buffers, ec);
        if (ec) throw boost::system::system_error(ec);
        return n;
    }
    template<class ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec) {
        return io(false, [&](boost::system::error_code& e) { return socket_.write_some(buffers, e); }, ec);
    }
    template<class ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers) {
        boost::system::error_code ec;
        const auto n = write_some(buffers, ec);
        if (ec) throw boost::system::system_error(ec);
        return n;
    }

private:
    std::size_t io(bool reading, const std::function<std::size_t(boost::system::error_code&)>& op,
                   boost::system::error_code& ec);

    tcp::socket& socket_;
    std::chrono::milliseconds timeout_{std::chrono::seconds(30)};
};

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket);

    // A session for an accepted socket, registered so cancelAllActive() reaches it even before a pool thread runs it.
    [[nodiscard]] static std::shared_ptr<Session> open(tcp::socket socket);

    void run();
    void cancel() noexcept;

    static void cancelAllActive() noexcept;
    // Defaults: 20 s keep-alive idle, 60 s read/write inactivity.
    static void setTimeoutsForTesting(std::chrono::milliseconds idle, std::chrono::milliseconds io);
    // True once every session object (and so every socket) is gone; false if the timeout passed first.
    [[nodiscard]] static bool waitUntilNoneAlive(std::chrono::milliseconds timeout);

private:
    bool read_one();
    bool handle_buffered_request(boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser);
    bool handle_streaming_upload(boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser);
    bool write_response(model::preview::Response&& response, bool headRequest = false);
    bool write_stream(model::preview::StreamResponse&& response);
    void do_close();

    static request make_request_from_parser(
        const boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser,
        std::string body = {}
    );

    SessionLifetimes::Token lifetime_;   // first: released after socket_ is destroyed
    tcp::socket socket_;
    TimedStream stream_{socket_};
    beast::flat_buffer buffer_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> nativeHandle_{-1};
    std::mutex socketMutex_;
};

}
