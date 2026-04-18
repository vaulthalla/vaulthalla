#pragma once

#include "concurrency/AsyncService.hpp"

#include <memory>
#include <boost/asio/ip/tcp.hpp>

namespace boost::asio { class io_context; }

namespace vh::protocols {

namespace ws { class Server; }
namespace http { class Server; }

class ConnectionLifecycleManager;

class ProtocolService final : public concurrency::AsyncService {
public:
    ProtocolService();

protected:
    void runLoop() override;

private:
    std::thread ioThread_;
    std::shared_ptr<boost::asio::io_context> ioContext_;
    std::shared_ptr<ws::Server> wsServer_;
    std::shared_ptr<http::Server> httpServer_;

    void initProtocols();
    void initWebsocketServer();
    void initHttpServer();
    static void initThreatIntelligence();
};

}
