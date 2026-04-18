#include "protocols/http/Session.hpp"
#include "protocols/http/Router.hpp"
#include "log/Registry.hpp"

namespace vh::protocols::http {

Session::Session(tcp::socket socket) : socket_(std::move(socket)) { buffer_.max_size(8192); }

void Session::run() {
    do_read();
}

void Session::do_read() {
    auto self = shared_from_this();

    http::async_read(socket_, buffer_, req_,
                     [self](beast::error_code ec, std::size_t bytes) {
                         self->on_read(ec, bytes);
                     });
}

void Session::on_read(beast::error_code ec, std::size_t bytes) {
    if (ec == http::error::end_of_stream) return do_close();

    if (ec) {
        log::Registry::http()->error("[Session] Read error: {}", ec.message());
        return;
    }

    log::Registry::http()->debug("[Session] Read {} bytes: {}", bytes, req_.target());

    auto self = shared_from_this();

    try {
        auto res = Router::route(std::move(req_));

        std::visit([self](auto&& response) {
            using T = std::decay_t<decltype(response)>;
            auto msg = std::make_shared<T>(std::forward<decltype(response)>(response));
            http::async_write(self->socket_, *msg,
                              [self, msg](beast::error_code ec, std::size_t bytes) {
                                  self->on_write(false, ec, bytes);
                              });
        }, std::move(res));
    } catch (const std::exception& e) {
        log::Registry::http()->error("[Session] Exception during request handling: {}", e.what());

        http::response<http::string_body> err{http::status::internal_server_error, req_.version()};
        err.set(http::field::content_type, "text/plain");
        err.body() = "Internal server error";
        err.prepare_payload();

        auto err_ptr = std::make_shared<decltype(err)>(std::move(err));
        http::async_write(socket_, *err_ptr,
            [self, err_ptr](beast::error_code, std::size_t) {
                self->on_write(true, {}, 0);
            });
    }
}

void Session::on_write(const bool close, beast::error_code ec, const std::size_t bytes) {
    (void)bytes; // unused

    if (ec) {
        log::Registry::http()->error("[Session] Write error: {}", ec.message());
        return;
    }

    if (close) {
        do_close();
        return;
    }

    // Clear buffer and start next request
    req_ = {};
    do_read();
}

void Session::do_close() {
    beast::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_send, ec);
    // ignore errors on shutdown
}

}
