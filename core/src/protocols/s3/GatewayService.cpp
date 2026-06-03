#include "protocols/s3/GatewayService.hpp"

#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "log/Registry.hpp"
#include "protocols/s3/Server.hpp"
#include "protocols/s3/Session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <stdexcept>

namespace vh::protocols::s3 {

namespace asio = boost::asio;

namespace {
asio::ip::address bindAddressForHost(const std::string& host) {
    if (host.empty() || host == "*") return asio::ip::make_address("0.0.0.0");
    if (host == "localhost") return asio::ip::make_address("127.0.0.1");

    boost::system::error_code ec;
    const auto address = asio::ip::make_address(host, ec);
    if (!ec) return address;

    throw std::runtime_error("S3 gateway host must be an IP address, localhost, or '*': " + host);
}
}

GatewayService::GatewayService() : AsyncService("S3GatewayService") {}

GatewayService::RuntimeStatus GatewayService::gatewayStatus() const noexcept {
    const auto metrics = Session::metrics();
    return {
        .running = isRunning(),
        .configured = configured_.load(std::memory_order_acquire),
        .ready = ready_.load(std::memory_order_acquire),
        .host = host_,
        .port = port_,
        .activeSessions = metrics.activeSessions,
        .totalRequests = metrics.totalRequests,
        .failedRequests = metrics.failedRequests
    };
}

void GatewayService::runLoop() {
    try {
        const auto& cfg = config::Registry::get().s3_gateway;
        host_ = cfg.host;
        port_ = cfg.port;
        configured_.store(cfg.enabled, std::memory_order_release);
        ready_.store(false, std::memory_order_release);

        if (!cfg.enabled) {
            log::Registry::runtime()->info("[S3GatewayService] Disabled in configuration.");
            while (!shouldStop()) lazySleep(std::chrono::seconds(1), std::chrono::milliseconds(100));
            return;
        }

        initGateway();

        while (!shouldStop())
            lazySleep(std::chrono::seconds(1), std::chrono::milliseconds(100));

        shutdownGateway();
    } catch (const std::exception& e) {
        log::Registry::runtime()->error("[S3GatewayService] Exception in run loop: {}", e.what());
        shutdownGateway();
        throw;
    } catch (...) {
        log::Registry::runtime()->error("[S3GatewayService] Unknown exception in run loop");
        shutdownGateway();
        throw;
    }
}

void GatewayService::onStop() {
    shutdownGateway();
}

void GatewayService::initGateway() {
    std::scoped_lock lock(lifecycleMutex_);
    const auto& cfg = config::Registry::get().s3_gateway;

    ioContext_ = std::make_shared<asio::io_context>();
    const auto address = bindAddressForHost(cfg.host);
    const auto endpoint = asio::ip::tcp::endpoint(address, cfg.port);

    server_ = std::make_shared<Server>(*ioContext_, endpoint, 1);
    server_->run();
    ready_.store(true, std::memory_order_release);

    ioThread_ = std::thread([ctx = ioContext_] {
        ctx->run();
    });

    log::Registry::runtime()->info("[S3GatewayService] Ready on {}:{}", cfg.host, cfg.port);
}

void GatewayService::shutdownGateway() noexcept {
    std::scoped_lock lock(lifecycleMutex_);
    try {
        ready_.store(false, std::memory_order_release);
        if (!server_ && !ioContext_ && !ioThread_.joinable()) return;

        if (server_) server_->close();
        Session::cancelAllActive();
        if (ioContext_) ioContext_->stop();
        if (ioThread_.joinable() && std::this_thread::get_id() != ioThread_.get_id()) ioThread_.join();
    } catch (const std::exception& e) {
        log::Registry::runtime()->error("[S3GatewayService] Shutdown failed: {}", e.what());
    } catch (...) {
        log::Registry::runtime()->error("[S3GatewayService] Shutdown failed: unknown exception");
    }

    server_.reset();
    ioContext_.reset();
}

} // namespace vh::protocols::s3
