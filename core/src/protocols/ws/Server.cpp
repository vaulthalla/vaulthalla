#include "protocols/ws/Server.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Handler.hpp"
#include "protocols/TCPAcceptor.hpp"
#include "runtime/Deps.hpp"
#include "auth/session/Manager.hpp"

namespace vh::protocols::ws {

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
    wrap_sys("[WebSocketServer] TCP_NODELAY set failed",
        [&] { socket.set_option(tcp::no_delay(true)); });

    wrap_sys("[WebSocketServer] KEEPALIVE set failed",
        [&] { socket.set_option(asio::socket_base::keep_alive(true)); });

    runtime::Deps::get().sessionManager->accept(std::move(socket), router_);
}

}
