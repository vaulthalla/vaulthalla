#include "protocols/ws/ConnectionLifecycleManager.hpp"
#include "auth/session/Manager.hpp"
#include "auth/session/Validator.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/model/Response.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "config/Registry.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/model/RefreshToken.hpp"

using namespace vh::protocols::ws;
using namespace vh::auth;
using namespace vh::config;
using namespace std::chrono;

ConnectionLifecycleManager::ConnectionLifecycleManager()
    : AsyncService("LifecycleManager") {
    const auto& config = Registry::get().services.connection_lifecycle_manager;
    sweep_interval_ = seconds(config.sweep_interval_seconds);
    unauthenticated_session_timeout_ = seconds(config.unauthenticated_timeout_seconds);
    idle_timeout_ = minutes(config.idle_timeout_minutes); // TODO: implement idle timeout logic
}

ConnectionLifecycleManager::~ConnectionLifecycleManager() {
    stop();
}

std::uint64_t ConnectionLifecycleManager::sweepIntervalSeconds() const noexcept {
    return static_cast<std::uint64_t>(sweep_interval_.count());
}

std::uint64_t ConnectionLifecycleManager::unauthenticatedSessionTimeoutSeconds() const noexcept {
    return static_cast<std::uint64_t>(unauthenticated_session_timeout_.count());
}

std::uint64_t ConnectionLifecycleManager::idleTimeoutMinutes() const noexcept {
    return static_cast<std::uint64_t>(idle_timeout_.count());
}

void ConnectionLifecycleManager::runLoop() {
    log::Registry::ws()->info("[LifecycleManager] Started connection lifecycle manager.");
    while (!shouldStop()) {
        sweepActiveSessions();
        lazySleep(sweep_interval_);
    }
}

void ConnectionLifecycleManager::sweepActiveSessions() const {
    for (const auto& [_, session] : runtime::Deps::get().sessionManager->getActive()) {
        try {
            if (!session) continue;

            if (!session->user && !session->isShareSession() &&
                session->connectionOpenedAt + unauthenticated_session_timeout_ < system_clock::now()) {
                log::Registry::ws()->debug("[LifecycleManager] Closing unauthenticated session (no token) opened at {}",
                                        system_clock::to_time_t(session->connectionOpenedAt));

                model::Response::UNAUTHORIZED("unauthenticated_session_timeout",
                    "Session closed due to inactivity. Please authenticate to continue.")(session);
                runtime::Deps::get().sessionManager->invalidate(session);
                session->close();
                continue;
            }

            const auto refreshToken = session->tokens
                                          ? (session->tokens->refreshToken
                                                 ? session->tokens->refreshToken
                                                 : session->tokens->shareRefreshToken)
                                          : nullptr;
            if (!refreshToken || !refreshToken->isValid()) {
                log::Registry::ws()->debug("[LifecycleManager] Closing session with expired refresh token (opened at {})",
                                        system_clock::to_time_t(session->connectionOpenedAt));

                model::Response::UNAUTHORIZED("unauthenticated_session_timeout",
                    "Session closed due to expired refresh token. Please re-authenticate to continue.")(session);
                runtime::Deps::get().sessionManager->invalidate(session);
                session->close();
                continue;
            }
        } catch (const std::exception& e) {
            log::Registry::ws()->error("[LifecycleManager] Error while sweeping sessions: {}", e.what());
        }
    }
}
