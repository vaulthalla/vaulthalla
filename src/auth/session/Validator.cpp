#include "auth/session/Validator.hpp"
#include "auth/session/Manager.hpp"
#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Issuer.hpp"
#include "protocols/ws/Session.hpp"
#include "log/Registry.hpp"
#include "crypto/util/hash.hpp"
#include "identities/model/User.hpp"
#include "runtime/Deps.hpp"
#include "db/query/auth/RefreshToken.hpp"

using namespace vh::protocols::ws;
using namespace vh::auth::model;

namespace vh::auth::session {

void Validator::validateRefreshToken(const std::shared_ptr<Session>& session) {
    if (!softValidateSession(session)) throw std::runtime_error("Session is not authenticated");

    const auto claims = Issuer::decode(session->tokens->refreshToken->rawToken);
    validateClaims(session->tokens->refreshToken, claims);

    if (claims->subject != Issuer::buildRefreshTokenSubject(session)) {
        log::Registry::ws()->debug("[Client] Refresh token subject mismatch: expected {}, got {}", Issuer::buildRefreshTokenSubject(session), claims->subject);
        throw std::runtime_error("Refresh token subject mismatch");
    }

    if (const auto priorSession = runtime::Deps::get().sessionManager->get(claims->jti))
        handlePriorSession(session, priorSession);

    // detect potential token reuse or tampering by comparing the incoming token with the stored token details
    if (const auto storedToken = db::query::auth::RefreshToken::get(claims->jti)) {
        checkForDangerousDiversion(session->tokens->refreshToken, storedToken);

        if (!crypto::hash::verifyPassword(session->tokens->refreshToken->rawToken, storedToken->hashedToken))
            throw std::runtime_error(
                "Refresh token hash mismatch");
    }

    if (const auto user = db::query::auth::RefreshToken::getUserByJti(claims->jti)) {
        if (user->id != session->tokens->refreshToken->userId) {
            runtime::Deps::get().sessionManager->invalidate(session);
            log::Registry::auth()->debug("[session::Resolver] User ID mismatch for JTI: {}, token user ID: {}, expected user ID: {}",
                claims->jti, session->tokens->refreshToken->userId, user->id);
            throw std::runtime_error("User ID mismatch for refresh token");
        }
    } else {
        runtime::Deps::get().sessionManager->invalidate(session);
        log::Registry::auth()->debug("[session::Resolver] No user found for JTI: {}, token user ID: {}",
            claims->jti, session->tokens->refreshToken->userId);
        throw std::runtime_error("No user found for refresh token");
    }
}

bool Validator::validateAccessToken(const std::shared_ptr<Session>& session, const std::string& accessToken) {
    try {
        if (!hasUsableAccessToken(session)) {
            log::Registry::ws()->debug("[Client] No valid access token found for session");
            return false;
        }

        if (session->tokens->accessToken->rawToken != accessToken) {
            log::Registry::ws()->debug("[Client] Access token mismatch for session");
            return false;
        }

        const auto claims = Issuer::decode(accessToken);
        validateClaims(session->tokens->accessToken, claims);
        if (claims->subject != Issuer::buildAccessTokenSubject(session)) {
            log::Registry::ws()->debug("[Client] Access token subject mismatch: expected {}, got {}", Issuer::buildAccessTokenSubject(session), claims->subject);
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        log::Registry::ws()->debug("[Client] Token validation failed: {}", e.what());
        return false;
    }
}

bool Validator::softValidateSession(const std::shared_ptr<Session>& session) {
    return session->user && hasUsableAccessToken(session) && hasUsableRefreshToken(session);
}

bool Validator::hasUsableAccessToken(const std::shared_ptr<Session>& session) {
    return session->tokens->accessToken && session->tokens->accessToken->isValid();
}

bool Validator::hasUsableRefreshToken(const std::shared_ptr<Session>& session) {
    return session->tokens->refreshToken && session->tokens->refreshToken->isValid();
}

void Validator::checkForDangerousDiversion(const std::shared_ptr<RefreshToken>& incomingToken, const std::shared_ptr<RefreshToken>& storedToken) {
    if (incomingToken->dangerousDivergence(storedToken)) {
        const auto msg = fmt::format("[session::Resolver] Dangerous divergence detected, raw tokens match but token details differ for JTI: {}, stored token: {}, incoming token: {}",
            incomingToken->jti,
            storedToken->rawToken,
            incomingToken->rawToken);
        log::Registry::ws()->warn(msg);
        log::Registry::audit()->warn(msg);
        throw std::runtime_error("Potential token tampering detected");
    }
}

void Validator::handlePriorSession(const std::shared_ptr<Session>& session, const std::shared_ptr<Session>& priorSession) {
    if (!softValidateSession(priorSession)) throw std::runtime_error("Prior session is not authenticated");
    log::Registry::auth()->debug("[session::Resolver] Rehydrating existing session for JTI: {}", priorSession->tokens->refreshToken->jti);
    if (priorSession->tokens->refreshToken != session->tokens->refreshToken) {
        checkForDangerousDiversion(session->tokens->refreshToken, priorSession->tokens->refreshToken);
        runtime::Deps::get().sessionManager->invalidate(priorSession);
        log::Registry::auth()->debug("[session::Resolver] Refresh token mismatch for JTI: {}", priorSession->tokens->refreshToken->jti);
        throw std::runtime_error("Refresh token mismatch");
    }
}

void Validator::validateClaims(const std::shared_ptr<Token>& t, const std::optional<TokenClaims>& claims) {
    if (!claims) throw std::runtime_error("Invalid refresh token: unable to decode claims");

    if (claims->expiresAt < std::chrono::system_clock::now()) throw std::runtime_error("Refresh token has expired");
    if (std::chrono::system_clock::to_time_t(claims->expiresAt) != t->expiresAt) throw std::runtime_error("Expiration time mismatch in refresh token");

    if (claims->jti.empty()) throw std::runtime_error("Missing JTI in refresh token");
    if (claims->jti != t->jti) throw std::runtime_error("JTI mismatch in refresh token");

    if (claims->subject.empty()) throw std::runtime_error("Missing subject in refresh token");
}

}
