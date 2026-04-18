#pragma once

#include "protocols/TCPAcceptor.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <memory>
#include <string_view>

namespace spdlog { class logger; }

namespace vh::protocols {

namespace asio  = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

enum class LogChannel { Http, WebSocket, General };

struct TcpServerOptions {
    unsigned int acceptConcurrency{1};
    bool useStrand{true};
    LogChannel channel{LogChannel::General};
};

class TCPServer : public std::enable_shared_from_this<TCPServer> {
public:
    TCPServer(asio::io_context& ioc, const tcp::endpoint& endpoint, TcpServerOptions opts);
    virtual ~TCPServer() = default;

    void run();

protected:
    virtual std::string_view serverName() const noexcept = 0;
    virtual void onAccept(tcp::socket socket) = 0;

    // Override if a server wants different accept error behavior
    virtual void onAcceptError(const beast::error_code& ec);

    asio::io_context& ioc() const noexcept { return ioc_; }
    tcp::acceptor& acceptor() noexcept { return acceptor_; }

    std::shared_ptr<spdlog::logger> logger() const;

private:
    void logStart() const;
    void doAccept();

    asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    TcpServerOptions opts_;
};

}
