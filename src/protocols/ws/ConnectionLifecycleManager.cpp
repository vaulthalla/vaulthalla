#include "protocols/ws/ConnectionLifecycleManager.hpp"
#include "auth/session/Manager.hpp"
#include "auth/model/Client.hpp"
#include "auth/Manager.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "config/ConfigRegistry.hpp"

using namespace vh::protocols::ws;
using namespace vh::auth;
using namespace vh::config;
using namespace std::chrono;

ConnectionLifecycleManager::ConnectionLifecycleManager()
    : AsyncService("LifecycleManager"),
      sessionManager_(runtime::Deps::get().authManager->sessionManager) {
    const auto& config = ConfigRegistry::get().services.connection_lifecycle_manager;
    sweep_interval_ = seconds(config.sweep_interval_seconds);
    unauthenticated_session_timeout_ = seconds(config.unauthenticated_timeout_seconds);
    idle_timeout_ = minutes(config.idle_timeout_minutes); // TODO: implement idle timeout logic
}

ConnectionLifecycleManager::~ConnectionLifecycleManager() {
    stop();
}

void ConnectionLifecycleManager::runLoop() {
    log::Registry::ws()->info("[LifecycleManager] Started connection lifecycle manager.");
    while (!shouldStop()) {
        sweepActiveSessions();
        lazySleep(sweep_interval_);
    }
}

void ConnectionLifecycleManager::sweepActiveSessions() const {
    for (const auto& [tokenStr, client] : sessionManager_->getActiveSessions()) {
        if (!client) continue;

        if (client->connOpenedAt() + unauthenticated_session_timeout_ < system_clock::now() && !client->tokens) {
            log::Registry::ws()->debug("[LifecycleManager] Closing unauthenticated session (no token) opened at {}",
                                    system_clock::to_time_t(client->connOpenedAt()));
            client->sendControlMessage("unauthenticated_session_timeout", {});
            client->closeConnection();
            sessionManager_->invalidateSession(tokenStr);
            continue;
        }

        if (!client->validateSession()) {
            if (const auto user = client->user) {
                log::Registry::ws()->debug("[LifecycleManager] Token revoked. Closing session for user {}", user->id);
                client->sendControlMessage("token_revoked", {});
            }
            client->closeConnection();
            sessionManager_->invalidateSession(tokenStr);
            continue;
        }

        const auto secondsLeft = client->tokens->getTimeLeft();

        if (secondsLeft <= 0) {
            if (const auto user = client->user) {
                log::Registry::ws()->debug("[LifecycleManager] Token expired. Closing session for user {}", user->id);
                client->sendControlMessage("token_expired", {});
            }
            client->closeConnection();
            sessionManager_->invalidateSession(tokenStr);
            continue;
        }

        // sendControlMessage requires a user to be set
        if (client->user) {
            if (secondsLeft <= 10)
                client->sendControlMessage("token_refresh_urgent", {{"deadline_ms", 10000}});
            else if (secondsLeft <= 300)
                client->sendControlMessage("token_refresh_requested", {{"deadline_ms", 300000}});
        }
    }
}
