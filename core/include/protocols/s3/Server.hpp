#pragma once

#include "protocols/TCPServer.hpp"

namespace vh::protocols::s3 {

namespace net = boost::asio;
using tcp = net::ip::tcp;

class Server final : public TCPServer {
public:
    Server(net::io_context& ioc, const tcp::endpoint& endpoint, unsigned int acceptConcurrency);

private:
    std::string_view serverName() const noexcept override { return "S3GatewayServer"; }
    void onAccept(tcp::socket socket) override;
};

} // namespace vh::protocols::s3
