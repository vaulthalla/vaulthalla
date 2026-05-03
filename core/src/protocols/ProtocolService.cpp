#include "protocols/ProtocolService.hpp"
#include "crypto/password/Strength.hpp"
#include "config/Registry.hpp"
#include "config/Config.hpp"
#include "protocols/ws/Server.hpp"
#include "protocols/http/Server.hpp"
#include "log/Registry.hpp"

#include <boost/asio/io_context.hpp>
#include <sodium.h>

namespace vh::protocols {

ProtocolService::ProtocolService() : AsyncService("ProtocolService") {}

ProtocolService::RuntimeStatus ProtocolService::protocolStatus() const noexcept {
    return {
        .running = isRunning(),
        .ioContextInitialized = ioContextInitialized_.load(std::memory_order_acquire),
        .websocketConfigured = websocketConfigured_.load(std::memory_order_acquire),
        .websocketReady = websocketReady_.load(std::memory_order_acquire),
        .httpPreviewConfigured = httpPreviewConfigured_.load(std::memory_order_acquire),
        .httpPreviewReady = httpPreviewReady_.load(std::memory_order_acquire)
    };
}

void ProtocolService::runLoop() {
    try {
        if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");
        initThreatIntelligence();
        initProtocols();

        while (!shouldStop()) std::this_thread::sleep_for(std::chrono::seconds(1));

        if (ioContext_) ioContext_->stop();
        if (ioThread_.joinable()) ioThread_.join();
        websocketReady_.store(false, std::memory_order_release);
        httpPreviewReady_.store(false, std::memory_order_release);
        ioContextInitialized_.store(false, std::memory_order_release);
    } catch (const std::exception& e) {
        log::Registry::runtime()->error("[ProtocolService] Exception in run loop: {}", e.what());
        if (ioContext_) ioContext_->stop();
        if (ioThread_.joinable()) ioThread_.join();
        websocketReady_.store(false, std::memory_order_release);
        httpPreviewReady_.store(false, std::memory_order_release);
        ioContextInitialized_.store(false, std::memory_order_release);
    } catch (...) {
        log::Registry::runtime()->error("[ProtocolService] Unknown exception in run loop");
        if (ioContext_) ioContext_->stop();
        if (ioThread_.joinable()) ioThread_.join();
        websocketReady_.store(false, std::memory_order_release);
        httpPreviewReady_.store(false, std::memory_order_release);
        ioContextInitialized_.store(false, std::memory_order_release);
    }
}

void ProtocolService::initProtocols() {
    const auto& cfg = vh::config::Registry::get();
    ioContextInitialized_.store(false, std::memory_order_release);
    websocketConfigured_.store(cfg.websocket.enabled, std::memory_order_release);
    httpPreviewConfigured_.store(cfg.http_preview.enabled, std::memory_order_release);
    websocketReady_.store(false, std::memory_order_release);
    httpPreviewReady_.store(false, std::memory_order_release);

    if (!cfg.websocket.enabled && !cfg.http_preview.enabled) {
        log::Registry::runtime()->warn(
            "[ProtocolService] Both WebSocket and HTTP preview servers are disabled in configuration. Nothing to start.");
        return;
    }

    ioContext_ = std::make_shared<asio::io_context>();
    ioContextInitialized_.store(true, std::memory_order_release);

    initWebsocketServer();
    initHttpServer();

    ioThread_ = std::thread([ctx = ioContext_] { ctx->run(); });
}


void ProtocolService::initWebsocketServer() {
    const auto& cfg = vh::config::Registry::get().websocket;
    if (!cfg.enabled) {
        log::Registry::runtime()->info("[ProtocolService] WebSocket server is disabled in configuration.");
        return;
    }

    const auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(cfg.host), cfg.port);
    wsServer_ = std::make_shared<ws::Server>(*ioContext_, endpoint);
    wsServer_->run();
    websocketReady_.store(true, std::memory_order_release);
}

void ProtocolService::initHttpServer() {
    const auto& cfg = vh::config::Registry::get().http_preview;
    if (!cfg.enabled) {
        log::Registry::runtime()->info("[ProtocolService] HTTP preview server is disabled in configuration.");
        return;
    }

    const auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(cfg.host), cfg.port);
    httpServer_ = std::make_shared<http::Server>(*ioContext_, endpoint);
    httpServer_->run();
    httpPreviewReady_.store(true, std::memory_order_release);
}

void ProtocolService::initThreatIntelligence() {
    vh::crypto::password::Strength::loadCommonWeakPasswordsFromURLs(
            {"https://raw.githubusercontent.com/danielmiessler/SecLists/refs/heads/master/Passwords/Common-Credentials/"
             "100k-most-used-passwords-NCSC.txt",
             "https://raw.githubusercontent.com/danielmiessler/SecLists/refs/heads/master/Passwords/Common-Credentials/"
             "probable-v2_top-12000.txt"});

    vh::crypto::password::Strength::loadDictionaryFromURL(
        "https://raw.githubusercontent.com/dolph/dictionary/refs/heads/master/popular.txt");
}

}
