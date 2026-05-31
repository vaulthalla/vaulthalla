#include "protocols/s3/Server.hpp"

#include "config/Registry.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "log/Registry.hpp"
#include "protocols/s3/Session.hpp"
#include "protocols/s3/task/AsyncSession.hpp"

namespace vh::protocols::s3 {

Server::Server(net::io_context& ioc, const tcp::endpoint& endpoint, const unsigned int acceptConcurrency)
    : TCPServer(ioc, endpoint, TcpServerOptions{
          .acceptConcurrency = acceptConcurrency == 0 ? 1u : acceptConcurrency,
          .useStrand = true,
          .channel = LogChannel::General
      }) {}

void Server::onAccept(tcp::socket socket) {
    const auto maxConnections = config::Registry::get().s3_gateway.max_connections;
    if (maxConnections > 0 && Session::metrics().activeSessions >= maxConnections) {
        log::Registry::runtime()->warn("[S3Gateway] Rejecting connection: max_connections={} reached", maxConnections);
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        ec.clear();
        socket.close(ec);
        return;
    }

    auto session = std::make_shared<Session>(std::move(socket));
    auto& pools = concurrency::ThreadPoolManager::instance();
    auto pool = pools.s3Pool();

    if (!pool) {
        log::Registry::runtime()->warn("[S3Gateway] s3Pool is unavailable; falling back to httpPool");
        pool = pools.httpPool();
    }

    if (!pool) {
        log::Registry::runtime()->error("[S3Gateway] No thread pool available for accepted S3 session");
        session->cancel();
        return;
    }

    pool->submit(std::make_unique<task::AsyncSession>(std::move(session)));
    pools.signalPressureChange();
}

} // namespace vh::protocols::s3
