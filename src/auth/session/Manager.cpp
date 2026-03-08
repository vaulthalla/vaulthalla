#include "auth/session/Manager.hpp"

#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/session/Validator.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "log/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "identities/model/User.hpp"

using namespace vh::protocols::ws;
using namespace vh::identities::model;

namespace vh::auth::session {

void Manager::tryRehydrate(const std::shared_ptr<Session>& session) {
    if (!session) throw std::invalid_argument("Invalid session");
    Validator::validateRefreshToken(session); // May throw if invalid
    cache(session);
}

void Manager::promote(const std::shared_ptr<Session>& session) {
    if (!session || !Validator::softValidateSession(session))
        throw std::invalid_argument("Invalid session state for promotion");

    const auto oldJti = session->tokens->refreshToken->jti;

    if (const auto dbToken = db::query::auth::RefreshToken::get(oldJti);
        dbToken->dangerousDivergence(session->tokens->refreshToken))
            invalidate(oldJti);

    db::query::auth::RefreshToken::set(session->tokens->refreshToken);

    session->tokens->refreshToken = db::query::auth::RefreshToken::get(oldJti);
    if (!session->tokens->refreshToken)
        throw std::runtime_error("Failed to reload refresh token");

    if (!oldJti.empty() && oldJti != session->tokens->refreshToken->jti)
        invalidate(oldJti);

    cache(session);

    log::Registry::ws()->debug(
        "[SessionManager] Promoted session for user: {}",
        session->user->name
    );
}

void Manager::cache(const std::shared_ptr<Session>& session) {
    if (!session) throw std::invalid_argument("Invalid session data for caching");

    std::lock_guard lock(sessionMutex_);

    sessionsByUUID_[session->uuid] = session;

    if (session->tokens && session->tokens->refreshToken && !session->tokens->refreshToken->jti.empty())
        sessionsByRefreshJti_[session->tokens->refreshToken->jti] = session;

    if (session->user)
        sessionsByUserId_.emplace(session->user->id, session);
}

bool Manager::validate(const std::shared_ptr<Session>& session, const std::string& accessToken) {
    try {
        if (!session) throw std::invalid_argument("Invalid session for validation");

        if (Validator::validateAccessToken(session, accessToken)) {
            if (session->tokens->accessToken->timeRemaining() < std::chrono::seconds(60)) Issuer::accessToken(session);
            cache(session);
            return true;
        }

        Validator::validateRefreshToken(session); // May throw if invalid
        Issuer::accessToken(session); // Assign new access token
        cache(session);
        return true;
    } catch (const std::exception& ex) {
        log::Registry::ws()->debug(
            "[SessionManager] Validation failed for session UUID: {}. Reason: {}",
            session ? session->uuid : "null",
            ex.what()
        );
        invalidate(session);
        return false;
    }
}

std::shared_ptr<Session> Manager::validateRawRefreshToken(const std::string& refreshToken) {
    const auto claims = Issuer::decode(refreshToken);
    if (!claims || claims->jti.empty()) throw std::invalid_argument("Invalid refresh token for validation");

    const auto session = get(claims->jti);
    if (!session) throw std::invalid_argument("No session found for refresh token validation");
    if (!session->tokens->refreshToken) throw std::invalid_argument("Session does not contain a refresh token for validation");

    if (session->tokens->refreshToken->rawToken.empty()) session->tokens->refreshToken->rawToken = refreshToken;
    else if (session->tokens->refreshToken->rawToken != refreshToken)
        throw std::invalid_argument("Refresh token mismatch for validation");

    Validator::validateRefreshToken(session);

    return session;
}

void Manager::invalidate(const std::shared_ptr<Session>& session) {
    if (!session) return;

    const auto uuid = session->uuid;
    const auto jti =
        (session->tokens && session->tokens->refreshToken)
            ? session->tokens->refreshToken->jti
            : std::string{};
    const auto userId =
        session->user ? std::optional<uint32_t>{session->user->id} : std::nullopt;

    if (session->tokens)
        session->tokens->invalidate();

    {
        std::lock_guard lock(sessionMutex_);
        sessionsByUUID_.erase(uuid);
        if (!jti.empty()) sessionsByRefreshJti_.erase(jti);

        if (userId) {
            auto [begin, end] = sessionsByUserId_.equal_range(*userId);
            for (auto it = begin; it != end;) {
                if (it->second == session) it = sessionsByUserId_.erase(it);
                else ++it;
            }
        }
    }

    log::Registry::ws()->debug(
        "[SessionManager] Invalidated session with UUID: {}",
        uuid
    );
}

void Manager::invalidate(const std::string& token) {
    invalidate(get(token));
}

std::shared_ptr<Session> Manager::get(const std::string& token) {
    std::lock_guard lock(sessionMutex_);

    if (sessionsByUUID_.contains(token)) return sessionsByUUID_[token];
    if (sessionsByRefreshJti_.contains(token)) return sessionsByRefreshJti_[token];

    return nullptr;
}

std::vector<std::shared_ptr<Session>> Manager::getSessions(const std::shared_ptr<User>& user) {
    if (!user) throw std::invalid_argument("Invalid user");
    return getSessionsByUserId(user->id);
}

std::vector<std::shared_ptr<Session>> Manager::getSessionsByUserId(const uint32_t userId) {
    std::lock_guard lock(sessionMutex_);

    std::vector<std::shared_ptr<Session>> sessions;
    const auto [begin, end] = sessionsByUserId_.equal_range(userId);

    for (auto it = begin; it != end; ++it)
        sessions.push_back(it->second);

    return sessions;
}

std::unordered_map<std::string, std::shared_ptr<Session>> Manager::getActive() {
    std::lock_guard lock(sessionMutex_);
    return sessionsByUUID_;
}

}
