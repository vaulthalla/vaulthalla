#include "protocols/http/Server.hpp"
#include "protocols/http/Session.hpp"
#include "config/Registry.hpp"
#include "log/Registry.hpp"

#include <atomic>
#include <string_view>
#include <thread>

namespace vh::protocols::http {

namespace {
std::atomic<unsigned int> activeConnections{0};

void refuseBusy(tcp::socket& socket) {
    static constexpr std::string_view kBusy =
        "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nRetry-After: 1\r\nConnection: close\r\n\r\n";
    boost::system::error_code ec;
    boost::asio::write(socket, boost::asio::buffer(kBusy.data(), kBusy.size()), ec);
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);
}
}

Server::Server(net::io_context& ioc, const tcp::endpoint& endpoint)
    : TCPServer(ioc, endpoint, TcpServerOptions{
          .acceptConcurrency = 1,
          .useStrand = true,
          .channel = LogChannel::Http
      }) {}

unsigned int Server::activeConnectionCount() { return activeConnections.load(); }

// One thread per connection, bounded by http_preview_server.max_connections. A long media stream or a slow
// download occupies only its own thread (with read/write deadlines), never a slot other requests are queued
// behind; CPU-heavy renders are separately gated (preview::RenderGate).
void Server::onAccept(tcp::socket socket) {
    const auto limit = std::max(1u, config::Registry::get().http_preview.max_connections);
    if (activeConnections.fetch_add(1) >= limit) {
        activeConnections.fetch_sub(1);
        log::Registry::http()->warn("[HttpServer] Connection limit ({}) reached; refusing with 503", limit);
        refuseBusy(socket);
        return;
    }

    auto session = Session::open(std::move(socket));
    try {
        std::thread([session = std::move(session)] {
            struct Release {
                ~Release() { activeConnections.fetch_sub(1); }
            } release;
            try {
                session->run();
            } catch (const std::exception& e) {
                log::Registry::http()->error("[HttpServer] Session terminated: {}", e.what());
            }
        }).detach();
    } catch (const std::exception& e) {
        activeConnections.fetch_sub(1);
        log::Registry::http()->error("[HttpServer] Could not start a connection thread: {}", e.what());
    }
}

}
