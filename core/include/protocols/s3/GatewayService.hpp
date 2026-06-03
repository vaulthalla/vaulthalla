#pragma once

#include "concurrency/AsyncService.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace boost::asio { class io_context; }

namespace vh::protocols::s3 {

class Server;

class GatewayService final : public concurrency::AsyncService {
public:
    struct RuntimeStatus {
        bool running = false;
        bool configured = false;
        bool ready = false;
        std::string host;
        uint16_t port = 0;
        uint64_t activeSessions = 0;
        uint64_t totalRequests = 0;
        uint64_t failedRequests = 0;
    };

    GatewayService();

    [[nodiscard]] RuntimeStatus gatewayStatus() const noexcept;

protected:
    void runLoop() override;
    void onStop() override;

private:
    mutable std::mutex lifecycleMutex_;
    std::shared_ptr<boost::asio::io_context> ioContext_;
    std::shared_ptr<Server> server_;
    std::thread ioThread_;
    std::atomic<bool> configured_{false};
    std::atomic<bool> ready_{false};
    std::string host_;
    uint16_t port_{0};

    void initGateway();
    void shutdownGateway() noexcept;
};

} // namespace vh::protocols::s3
