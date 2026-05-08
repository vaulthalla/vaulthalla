#include "protocols/TCPServer.hpp"
#include "log/Registry.hpp"

#include <utility>

namespace vh::protocols {

TCPServer::TCPServer(asio::io_context& ioc,
                             const tcp::endpoint& endpoint,
                             const TcpServerOptions opts)
    : ioc_(ioc), acceptor_(ioc), opts_(opts) { init_acceptor(acceptor_, endpoint); }

void TCPServer::run() {
    stopping_.store(false, std::memory_order_release);
    logStart();

    const auto n = (opts_.acceptConcurrency == 0) ? 1u : opts_.acceptConcurrency;
    for (unsigned int i = 0; i < n; ++i) doAccept();
}

void TCPServer::close() noexcept {
    stopping_.store(true, std::memory_order_release);
    beast::error_code ec;
    acceptor_.cancel(ec);
    ec.clear();
    acceptor_.close(ec);
}

void TCPServer::onAcceptError(const beast::error_code& ec) {
    logger()->debug("[{}] accept error: {}", serverName(), ec.message());
}

std::shared_ptr<spdlog::logger> TCPServer::logger() const {
    switch (opts_.channel) {
    case LogChannel::Http:      return log::Registry::http();
    case LogChannel::WebSocket: return log::Registry::ws();
    case LogChannel::General:
    default: return log::Registry::runtime();
    }
}

void TCPServer::logStart() const {
    logger()->info("[{}] Starting at {}", serverName(), endpointToString(acceptor_));
}

void TCPServer::doAccept() {
    if (stopping_.load(std::memory_order_acquire) || !acceptor_.is_open()) return;

    auto self = shared_from_this();

    auto handler = [self](const beast::error_code& ec, tcp::socket socket) mutable {
        if (ec) {
            if (ec == asio::error::operation_aborted || self->stopping()) return;
            self->onAcceptError(ec);
            self->doAccept();
            return;
        }

        if (self->stopping()) return;
        self->onAccept(std::move(socket));
        self->doAccept();
    };

    if (opts_.useStrand) acceptor_.async_accept(asio::make_strand(ioc_), std::move(handler));
    else acceptor_.async_accept(std::move(handler));
}

}
