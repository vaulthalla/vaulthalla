#pragma once

#include "protocols/TCPServer.hpp"

namespace vh::protocols::http {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class Server final : public TCPServer {
public:
    Server(net::io_context& ioc, const tcp::endpoint& endpoint);

private:
    std::string_view serverName() const noexcept override { return "HttpServer"; }
    void onAccept(tcp::socket socket) override;
};

}
