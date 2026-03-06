#include "auth/SessionManager.hpp"
#include "auth/model/RefreshToken.hpp"
#include "db/query/identities/User.hpp"
#include "log/Registry.hpp"

using namespace vh::auth;
using namespace vh::auth::model;

void SessionManager::createSession(const std::shared_ptr<Client>& client) {
    std::lock_guard lock(sessionMutex_);

    if (!client || !client->getSession()) throw std::invalid_argument("Session must not be null");

    if (const auto rt = client->getRefreshToken(); rt && !rt->getJti().empty())
        sessionsByRefreshJti_[rt->getJti()] = client;
    else throw std::invalid_argument("Refresh token not found");

    if (const auto user = client->getUser()) sessionsByUserId_[user->id] = client;
    sessionsByUUID_[client->getSession()->getUUID()] = client;

    log::Registry::ws()->debug("[SessionManager] Created session for user: {}", client->getUserName());
}

std::string SessionManager::promoteSession(const std::shared_ptr<Client>& client) {
    try {
        std::lock_guard lock(sessionMutex_);

        if (!client || !client->getSession()) throw std::invalid_argument("Client and session must not be null");

        const auto refreshToken = client->getRefreshToken();
        refreshToken->setUserId(client->getUser()->id);
        refreshToken->setUserAgent(client->getSession()->getUserAgent());
        refreshToken->setIpAddress(client->getSession()->getClientIp());

        if (!client->getUser()) throw std::invalid_argument("User must not be null");

        if (const auto dbToken = db::query::identities::User::getRefreshToken(refreshToken->getJti())) {
            if (dbToken->getUserId() != client->getUser()->id
                || dbToken->getUserAgent() != client->getSession()->getUserAgent())
                throw std::invalid_argument("Invalid refresh token");
        } else db::query::identities::User::addRefreshToken(refreshToken);

        const std::string oldJti = client->getRefreshToken() ? client->getRefreshToken()->getJti() : "";

        client->setRefreshToken(db::query::identities::User::getRefreshToken(refreshToken->getJti()));
        sessionsByUUID_[client->getSession()->getUUID()] = client;

        const std::string newJti = client->getRefreshToken()->getJti();

        if (!oldJti.empty() && oldJti != newJti) sessionsByRefreshJti_.erase(oldJti);
        sessionsByRefreshJti_[newJti] = client;
        sessionsByUserId_[client->getUser()->id] = client;

        log::Registry::ws()->debug("[SessionManager] Promoted session for user: {}", client->getUserName());
        return client->getRawToken();
    } catch (const std::exception& e) {
        log::Registry::ws()->error("[SessionManager] Failed to promote session: {}", e.what());
        return "";
    }
}

std::shared_ptr<Client> SessionManager::getClientSession(const std::string& token) {
    std::lock_guard lock(sessionMutex_);
    if (sessionsByUUID_.contains(token)) return sessionsByUUID_[token];
    if (sessionsByRefreshJti_.contains(token)) return sessionsByRefreshJti_[token];
    if (sessionsByUserId_.contains(std::stoi(token))) return sessionsByUserId_[std::stoi(token)];
    return nullptr;
}

void SessionManager::invalidateSession(const std::string& token) {
    std::lock_guard lock(sessionMutex_);

    std::shared_ptr<Client> client = nullptr;

    if (sessionsByUUID_.contains(token)) client = sessionsByUUID_[token];
    else if (sessionsByRefreshJti_.contains(token)) client = sessionsByRefreshJti_[token];
    else if (sessionsByUserId_.contains(std::stoi(token))) client = sessionsByUserId_[std::stoi(token)];
    else {
        log::Registry::ws()->warn("[SessionManager] No session found for token: {}", token);
        return;
    }

    if (!client || !client->getSession()) {
        log::Registry::ws()->warn("[SessionManager] Session not found for token: {}", token);
        return;
    }

    sessionsByUUID_.erase(client->getSession()->getUUID());
    sessionsByRefreshJti_.erase(client->getRefreshToken()->getJti());

    if (const auto user = client->getUser()) {
        sessionsByUserId_.erase(user->id);
        client->invalidateToken();
        db::query::identities::User::revokeAndPurgeRefreshTokens(user->id);
        log::Registry::ws()->debug("[SessionManager] Invalidated session: {}", token);
    }
}

std::unordered_map<std::string, std::shared_ptr<Client> > SessionManager::getActiveSessions() {
    std::lock_guard lock(sessionMutex_);
    return sessionsByUUID_;
}
