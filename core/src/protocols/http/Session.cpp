#include "protocols/http/Session.hpp"

#include "log/Registry.hpp"
#include "protocols/http/Router.hpp"
#include "protocols/http/upload/Coordinator.hpp"

#include <algorithm>
#include <array>
#include <boost/system/system_error.hpp>
#include <chrono>
#include <limits>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <variant>
#include <vector>

namespace vh::protocols::http {
namespace {
constexpr std::size_t kReadBufferBytes = 64u * 1024u;
constexpr std::size_t kMaxBufferedBodyBytes = 16u * 1024u * 1024u;
constexpr std::chrono::seconds kHttpSocketIdleTimeout{30};

[[nodiscard]] std::mutex& activeSessionsMutex() {
    static std::mutex value;
    return value;
}

[[nodiscard]] std::vector<std::weak_ptr<Session>>& activeSessions() {
    static std::vector<std::weak_ptr<Session>> value;
    return value;
}

void pruneActiveSessionsLocked() {
    auto& sessions = activeSessions();
    sessions.erase(
        std::remove_if(sessions.begin(), sessions.end(), [](const std::weak_ptr<Session>& item) {
            return item.expired();
        }),
        sessions.end()
    );
}

void registerActiveSession(const std::shared_ptr<Session>& session) {
    std::scoped_lock lock(activeSessionsMutex());
    pruneActiveSessionsLocked();
    activeSessions().push_back(session);
}

void unregisterActiveSession(const Session* session) {
    std::scoped_lock lock(activeSessionsMutex());
    auto& sessions = activeSessions();
    sessions.erase(
        std::remove_if(sessions.begin(), sessions.end(), [session](const std::weak_ptr<Session>& item) {
            const auto locked = item.lock();
            return !locked || locked.get() == session;
        }),
        sessions.end()
    );
}

void setSocketTimeouts(const int fd) noexcept {
    if (fd < 0) return;
    timeval timeout{
        .tv_sec = static_cast<decltype(timeval::tv_sec)>(kHttpSocketIdleTimeout.count()),
        .tv_usec = 0
    };
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

[[nodiscard]] status statusForException(const std::exception& e) {
    const std::string message = e.what();
    if (message.contains("token") || message.contains("Unauthorized") || message.contains("requires a ready share session") ||
        message.contains("requires a user session"))
        return status::unauthorized;
    if (message.contains("denied") || message.contains("Permission")) return status::forbidden;
    if (message.contains("not found")) return status::not_found;
    if (message.contains("exceeds") || message.contains("too large")) return status::payload_too_large;
    return status::bad_request;
}
} // namespace

Session::Session(tcp::socket socket) : socket_(std::move(socket)) {
    buffer_.max_size(8192);
    nativeHandle_.store(socket_.native_handle(), std::memory_order_release);
    setSocketTimeouts(nativeHandle_.load(std::memory_order_acquire));
}

void Session::run() {
    const auto self = shared_from_this();
    registerActiveSession(self);
    while (!stopRequested_.load(std::memory_order_acquire) && read_one()) {}
    do_close();
    unregisterActiveSession(this);
}

void Session::cancel() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    std::scoped_lock lock(socketMutex_);
    const auto fd = nativeHandle_.load(std::memory_order_acquire);
    if (fd >= 0) (void)::shutdown(fd, SHUT_RDWR);
}

void Session::cancelAllActive() noexcept {
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::scoped_lock lock(activeSessionsMutex());
        pruneActiveSessionsLocked();
        sessions.reserve(activeSessions().size());
        for (const auto& item : activeSessions()) {
            if (auto session = item.lock()) sessions.push_back(std::move(session));
        }
    }

    for (const auto& session : sessions) {
        if (session) session->cancel();
    }
}

request Session::make_request_from_parser(
    const boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser,
    std::string body
) {
    const auto& src = parser.get();
    request req{src.method(), src.target(), src.version()};
    for (const auto& field : src)
        req.insert(field.name_string(), field.value());
    req.keep_alive(src.keep_alive());
    req.body() = std::move(body);
    return req;
}

bool Session::read_one() {
    if (stopRequested_.load(std::memory_order_acquire)) return false;

    boost::beast::http::request_parser<boost::beast::http::buffer_body> parser;
    parser.body_limit(std::numeric_limits<uint64_t>::max());

    beast::error_code ec;
    boost::beast::http::read_header(socket_, buffer_, parser, ec);
    if (ec == boost::beast::http::error::end_of_stream || ec == boost::asio::error::eof) return false;
    if (ec) {
        if (!stopRequested_.load(std::memory_order_acquire))
            log::Registry::http()->error("[Session] Header read error: {}", ec.message());
        return false;
    }

    log::Registry::http()->debug("[Session] Request headers: {}", parser.get().target());

    try {
        if (upload::Coordinator::isUploadFileRequest(parser.get().method(), parser.get().target()))
            return handle_streaming_upload(parser);
        return handle_buffered_request(parser);
    } catch (const std::exception& e) {
        if (stopRequested_.load(std::memory_order_acquire)) return false;
        log::Registry::http()->error("[Session] Exception during request handling: {}", e.what());
        auto req = make_request_from_parser(parser);
        return write_response(Router::makeErrorResponse(req, e.what(), statusForException(e))) && req.keep_alive();
    }
}

bool Session::handle_buffered_request(
    boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser
) {
    std::string body;
    std::array<char, kReadBufferBytes> storage{};
    beast::error_code ec;

    while (!parser.is_done() && !stopRequested_.load(std::memory_order_acquire)) {
        parser.get().body().data = storage.data();
        parser.get().body().size = storage.size();
        boost::beast::http::read(socket_, buffer_, parser, ec);

        const auto used = storage.size() - parser.get().body().size;
        if (used > 0) {
            if (body.size() > kMaxBufferedBodyBytes - used)
                throw std::runtime_error("Request body exceeds maximum buffered size");
            body.append(storage.data(), used);
        }

        if (ec == boost::beast::http::error::need_buffer) {
            ec = {};
            continue;
        }
        if (ec) throw boost::system::system_error(ec);
    }
    if (stopRequested_.load(std::memory_order_acquire)) return false;

    auto req = make_request_from_parser(parser, std::move(body));
    const auto keepAlive = req.keep_alive();
    auto response = Router::route(std::move(req));
    return write_response(std::move(response)) && keepAlive;
}

bool Session::handle_streaming_upload(
    boost::beast::http::request_parser<boost::beast::http::buffer_body>& parser
) {
    auto req = make_request_from_parser(parser);
    const auto keepAlive = req.keep_alive();

    upload::Coordinator::FileStream stream;
    try {
        const auto contentLength = parser.content_length();
        stream = upload::Coordinator::instance().beginFile(
            req,
            contentLength ? std::make_optional(static_cast<uint64_t>(*contentLength)) : std::nullopt
        );

        std::array<char, kReadBufferBytes> storage{};
        beast::error_code ec;
        while (!parser.is_done() && !stopRequested_.load(std::memory_order_acquire)) {
            parser.get().body().data = storage.data();
            parser.get().body().size = storage.size();
            boost::beast::http::read(socket_, buffer_, parser, ec);

            const auto used = storage.size() - parser.get().body().size;
            if (used > 0) stream.write(storage.data(), used);

            if (ec == boost::beast::http::error::need_buffer) {
                ec = {};
                continue;
            }
            if (ec) throw boost::system::system_error(ec);
        }
        if (stopRequested_.load(std::memory_order_acquire)) throw std::runtime_error("http_session_cancelled");

        auto data = stream.finish();
        return write_response(Router::makeJsonResponse(req, data)) && keepAlive;
    } catch (const std::exception& e) {
        stream.fail(e.what());
        if (stopRequested_.load(std::memory_order_acquire)) return false;
        auto response = Router::makeErrorResponse(req, e.what(), statusForException(e));
        std::visit([](auto& res) { res.keep_alive(false); }, response);
        (void)write_response(std::move(response));
        return false;
    }
}

bool Session::write_response(model::preview::Response&& response) {
    if (stopRequested_.load(std::memory_order_acquire)) return false;

    beast::error_code ec;
    std::visit([this, &ec](auto&& res) {
        boost::beast::http::write(socket_, res, ec);
    }, response);

    if (ec) {
        log::Registry::http()->error("[Session] Write error: {}", ec.message());
        return false;
    }
    return true;
}

void Session::do_close() {
    stopRequested_.store(true, std::memory_order_release);
    std::scoped_lock lock(socketMutex_);
    nativeHandle_.store(-1, std::memory_order_release);
    beast::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    ec.clear();
    socket_.close(ec);
}

} // namespace vh::protocols::http
