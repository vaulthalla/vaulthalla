#include "auth/session/Manager.hpp"

#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/session/Validator.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "log/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "identities/User.hpp"
#include "runtime/Deps.hpp"
#include "protocols/ws/Router.hpp"

using namespace vh::protocols::ws;
using namespace vh::identities;

namespace vh::auth::session {

void Manager::accept(boost::asio::ip::tcp::socket&& socket, const std::shared_ptr<Router>& router) {
    const auto session = std::make_shared<Session>(router);
    cache(session);
    session->accept(std::move(socket));
}

void Manager::tryRehydrate(const std::shared_ptr<Session>& session) {
    try {
        if (!session) return;

        Validator::validateRefreshToken(session); // May throw if invalid
        Issuer::accessToken(session); // Assign new access token
        cache(session);

        if (!session->user) {
            Issuer::refreshToken(session);
            cache(session);
        }
    } catch (const std::exception& ex) {
        log::Registry::ws()->debug(
            "[session::Manager] Rehydration failed for session UUID: {}. Reason: {}",
            session ? session->uuid : "null",
            ex.what()
        );

        Issuer::refreshToken(session);
        cache(session);
    }
}

void Manager::promote(const std::shared_ptr<Session>& session) {
    Issuer::accessToken(session);

    if (!session || !Validator::softValidateActiveSession(session)) {
        log::Registry::ws()->debug("[session::Manager] Promotion failed due to invalid session data");
        throw std::invalid_argument("Invalid session data for promotion");
    }

    session->tokens->refreshToken->userId = session->user->id;

    if (const auto dbToken = db::query::auth::RefreshToken::get(session->tokens->refreshToken->jti)) {
        session->tokens->refreshToken = dbToken;
        cache(session);
        log::Registry::ws()->debug("[session::Manager] Promoted session for user: {} (existing refresh token found)", session->user->name);
        return;
    }

    db::query::auth::RefreshToken::set(session->tokens->refreshToken);

    session->tokens->refreshToken = db::query::auth::RefreshToken::get(session->tokens->refreshToken->jti);
    if (!session->tokens->refreshToken) {
        log::Registry::ws()->debug(
            "[session::Manager] Promotion failed: Unable to retrieve updated refresh token for session UUID: {}",
            session->uuid
        );
        throw std::runtime_error("Failed to retrieve updated refresh token during promotion");
    }

    cache(session);

    log::Registry::ws()->debug(
        "[session::Manager] Promoted session for user: {}",
        session->user->name
    );
}

void Manager::renewAccessToken(const std::shared_ptr<Session>& session, const std::string& existingToken) {
    if (!session) throw std::invalid_argument("Invalid session for access token renewal");
    if (!session->user) throw std::invalid_argument("Session does not contain user data for access token renewal");

    if (!Validator::validateAccessToken(session, existingToken)) {
        log::Registry::ws()->debug(
            "[session::Manager] Access token renewal failed for session UUID: {}. Reason: Invalid existing access token",
            session->uuid
        );

        try { Validator::validateRefreshToken(session); }
        catch (const std::exception& ex) {
            log::Registry::ws()->debug(
                "[session::Manager] Access token renewal failed for session UUID: {}. Reason: Refresh token validation failed during access token renewal. Exception: {}",
                session->uuid,
                ex.what()
            );
            throw std::invalid_argument("Invalid refresh token during access token renewal");
        }
    }

    Issuer::accessToken(session);
    cache(session);

    log::Registry::ws()->debug(
        "[session::Manager] Renewed access token for session UUID: {}",
        session->uuid
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
        if (!session->user) throw std::invalid_argument("Session does not contain user data for validation");

        if (Validator::validateAccessToken(session, accessToken)) {
            if (session->tokens->accessToken->timeRemaining() < std::chrono::minutes(5)) Issuer::accessToken(session);
            cache(session);
            return true;
        }

        Validator::validateRefreshToken(session); // May throw if invalid
        Issuer::accessToken(session); // Assign new access token
        cache(session);
        return true;
    } catch (const std::exception& ex) {
        log::Registry::ws()->debug(
            "[session::Manager] Validation failed for session UUID: {}. Reason: {}",
            session ? session->uuid : "null",
            ex.what()
        );
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
        "[session::Manager] Invalidated session with UUID: {}",
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
