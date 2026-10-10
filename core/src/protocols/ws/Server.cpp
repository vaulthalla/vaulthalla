#include "protocols/ws/Server.hpp"
#include "protocols/ws/ConnectionLimit.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Handler.hpp"
#include "protocols/TCPAcceptor.hpp"
#include "runtime/Deps.hpp"
#include "auth/session/Manager.hpp"
#include "config/Registry.hpp"
#include "log/Registry.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace vh::protocols::ws {

namespace {
// One warning per minute while connections are being refused, with the count since the last one: a flood must not
// turn into a log flood.
void warnRefused(const unsigned int limit) {
    static std::atomic<int64_t> lastWarnSeconds{0};
    static std::atomic<uint64_t> refusedAtLastWarn{0};
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = lastWarnSeconds.load(std::memory_order_relaxed);
    if (last != 0 && now - last < 60) return;
    if (!lastWarnSeconds.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    const auto total = ConnectionSlot::refused();
    const auto since = total - refusedAtLastWarn.exchange(total, std::memory_order_relaxed);
    log::Registry::ws()->warn(
        "[WebSocketServer] Connection limit (websocket_server.max_connections = {}) reached: refused {} connection(s) "
        "with 503 since the last warning",
        limit, since);
}
}

Server::Server(asio::io_context& ioc, const tcp::endpoint& endpoint)
    : TCPServer(ioc, endpoint, TcpServerOptions{
          .acceptConcurrency = 4,
          .useStrand = true,
          .channel = LogChannel::WebSocket
      })
    , router_(std::make_shared<Router>()) {
    Handler::registerAllHandlers(router_);
}

void Server::onAccept(tcp::socket socket) {
    const auto limit = config::Registry::get().websocket.max_connections;
    auto slot = ConnectionSlot::tryAcquire(limit);
    if (!slot) {
        warnRefused(std::max(1u, limit));
        refuseOverCapacity(socket);
        return;
    }

    wrap_sys("[WebSocketServer] TCP_NODELAY set failed",
        [&] { socket.set_option(tcp::no_delay(true)); });

    wrap_sys("[WebSocketServer] KEEPALIVE set failed",
        [&] { socket.set_option(asio::socket_base::keep_alive(true)); });

    runtime::Deps::get().sessionManager->accept(std::move(socket), router_, std::move(slot));
}

}
