#pragma once

#include "concurrency/AsyncService.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <boost/asio/ip/tcp.hpp>

namespace boost::asio { class io_context; }

namespace vh::protocols {

namespace ws { class Server; }
namespace http { class Server; }

class ConnectionLifecycleManager;

class ProtocolService final : public concurrency::AsyncService {
public:
    struct RuntimeStatus {
        bool running = false;
        bool ioContextInitialized = false;
        bool websocketConfigured = false;
        bool websocketReady = false;
        bool httpPreviewConfigured = false;
        bool httpPreviewReady = false;
    };

    ProtocolService();
    [[nodiscard]] RuntimeStatus protocolStatus() const noexcept;

protected:
    void runLoop() override;
    void onStop() override;

private:
    std::thread ioThread_;
    std::mutex lifecycleMutex_;
    std::shared_ptr<boost::asio::io_context> ioContext_;
    std::shared_ptr<ws::Server> wsServer_;
    std::shared_ptr<http::Server> httpServer_;
    std::atomic<bool> ioContextInitialized_{false};
    std::atomic<bool> websocketConfigured_{false};
    std::atomic<bool> websocketReady_{false};
    std::atomic<bool> httpPreviewConfigured_{false};
    std::atomic<bool> httpPreviewReady_{false};

    void initProtocols();
    void initWebsocketServer();
    void initHttpServer();
    void shutdownProtocols() noexcept;
    static void initThreatIntelligence();
};

}
