#pragma once
#include "protocols/TCPServer.hpp"
#include <memory>

namespace vh::protocols::ws {

class Router;

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class Server final : public TCPServer {
public:
    Server(asio::io_context& ioc, const tcp::endpoint& endpoint);

private:
    std::string_view serverName() const noexcept override { return "WebSocketServer"; }
    void onAccept(tcp::socket socket) override;

    std::shared_ptr<Router> router_;
};

}
