#include "auth/session/Manager.hpp"
#include "auth/session/Validator.hpp"
#include "protocols/ws/Session.hpp"
#include "auth/model/RefreshToken.hpp"
#include "db/query/identities/User.hpp"
#include "log/Registry.hpp"

using namespace vh::protocols::ws;

namespace vh::auth::session {

void Manager::ensureSession(const std::shared_ptr<Session>& session) {
    std::lock_guard lock(sessionMutex_);

    if (!session) throw std::invalid_argument("Invalid client or session data");

    sessionsByRefreshJti_[session->refreshToken->jti] = session;
    sessionsByUUID_[session->uuid] = session;
    if (const auto user = session->user) sessionsByUserId_[user->id] = session; // unlikely but just in case

    log::Registry::ws()->debug("[SessionManager] Created new unauthenticated session with UUID: {}", session->uuid);
}

std::string Manager::promoteSession(const std::shared_ptr<Session>& session) {
    if (!session || !Validator::validateSession(session))
        throw std::invalid_argument("Invalid client state for session promotion");

    const auto oldJti = session->refreshToken->jti;
    const auto userId = session->user->id;
    const auto userAgent = session->userAgent;
    const auto ipAddress = session->ipAddress;

    const auto token = session->refreshToken;
    token->userId = userId;
    token->userAgent = userAgent;
    token->ipAddress = ipAddress;

    if (const auto dbToken = db::query::identities::User::getRefreshToken(oldJti)) {
        if (dbToken->userId != userId || dbToken->userAgent != userAgent)
            throw std::invalid_argument("Invalid refresh token");
    } else db::query::identities::User::addRefreshToken(token);

    session->refreshToken = db::query::identities::User::getRefreshToken(oldJti);
    if (!session->refreshToken) throw std::runtime_error("Failed to reload refresh token");

    {
        std::lock_guard lock(sessionMutex_);

        const auto newJti = session->refreshToken->jti;

        if (!oldJti.empty() && oldJti != newJti)
            sessionsByRefreshJti_.erase(oldJti);

        sessionsByRefreshJti_[newJti] = session;
        sessionsByUUID_[session->uuid] = session;
        sessionsByUserId_[userId] = session;
    }

    log::Registry::ws()->debug("[SessionManager] Promoted session for user: {}", session->user->name);
    return session->tokens->rawToken;
}

std::shared_ptr<Session> Manager::getSession(const std::string& token) {
    std::lock_guard lock(sessionMutex_);
    if (sessionsByUUID_.contains(token)) return sessionsByUUID_[token];
    if (sessionsByRefreshJti_.contains(token)) return sessionsByRefreshJti_[token];
    if (sessionsByUserId_.contains(std::stoi(token))) return sessionsByUserId_[std::stoi(token)];
    return nullptr;
}

void Manager::invalidateSession(const std::string& token) {
    const auto session = getSession(token);

    if (!session) {
        log::Registry::ws()->warn("[SessionManager] Session not found for token: {}", token);
        return;
    }

    const auto user = session->user;

    {
        std::lock_guard lock(sessionMutex_);
        sessionsByUUID_.erase(session->uuid);
        sessionsByRefreshJti_.erase(session->refreshToken->jti);
        if (user) sessionsByUserId_.erase(user->id);
    }

    if (user) {
        session->tokens->revoke();
        session->refreshToken->revoke();
        db::query::identities::User::revokeAndPurgeRefreshTokens(user->id);
        log::Registry::ws()->debug("[SessionManager] Invalidated session: {}", token);
    }
}

std::unordered_map<std::string, std::shared_ptr<Session> > Manager::getActiveSessions() {
    std::lock_guard lock(sessionMutex_);
    return sessionsByUUID_;
}

}
