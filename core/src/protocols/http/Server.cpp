#include "protocols/http/Server.hpp"
#include "protocols/http/Session.hpp"
#include "protocols/http/task/AsyncSession.hpp"
#include "concurrency/ThreadPoolManager.hpp"

using namespace vh::concurrency;

namespace vh::protocols::http {

Server::Server(net::io_context& ioc, const tcp::endpoint& endpoint)
    : TCPServer(ioc, endpoint, TcpServerOptions{
          .acceptConcurrency = 1,
          .useStrand = true,
          .channel = LogChannel::Http
      }) {}

void Server::onAccept(tcp::socket socket) {
    auto session = std::make_shared<Session>(std::move(socket));
    ThreadPoolManager::instance().httpPool()->submit(std::make_unique<task::AsyncSession>(session));
}

}
