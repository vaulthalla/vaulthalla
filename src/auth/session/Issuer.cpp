#include "auth/session/Issuer.hpp"
#include "auth/model/RefreshToken.hpp"
#include "config/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "crypto/util/hash.hpp"
#include "crypto/secrets/Manager.hpp"
#include "identities/model/User.hpp"
#include "log/Registry.hpp"
#include "auth/model/TokenPair.hpp"

#include <chrono>
#include <uuid/uuid.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

using namespace vh::protocols::ws;

namespace vh::auth::session {

static std::string generateUUID() {
    uuid_t uuid;
    char uuidStr[37];
    uuid_generate(uuid);
    uuid_unparse(uuid, uuidStr);
    return {uuidStr};
}

static void finalizeAndSignToken(const std::shared_ptr<Session>& session, const std::shared_ptr<model::Token>& t, const std::chrono::system_clock::duration ttl) {
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(
        std::chrono::system_clock::now()
    );
    const auto exp = now + std::chrono::duration_cast<std::chrono::seconds>(ttl);

    t->issuedAt = now;
    t->expiresAt = exp;
    t->jti = generateUUID();
    if (session->user) t->userId = session->user->id;

    t->rawToken =
        jwt::create<jwt::traits::nlohmann_json>()
        .set_issuer("Vaulthalla")
        .set_subject(t->subject)
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_id(t->jti)
        .sign(jwt::algorithm::hs256{vh::runtime::Deps::get().secretsManager->jwtSecret()});
}

void Issuer::accessToken(const std::shared_ptr<Session>& session) {
    const auto t = std::make_shared<model::Token>();
    t->subject = buildAccessTokenSubject(session);
    finalizeAndSignToken(session, t, std::chrono::minutes(vh::config::Registry::get().auth.access_token_expiry_minutes));
    session->tokens->accessToken = t;
    session->sendAccessTokenOnNextResponse();
}

void Issuer::refreshToken(const std::shared_ptr<Session>& session) {
    const auto t = std::make_shared<model::RefreshToken>();
    t->subject = buildRefreshTokenSubject(session);
    t->userAgent = session->userAgent;
    t->ipAddress = session->ipAddress;
    finalizeAndSignToken(session, t, std::chrono::days(vh::config::Registry::get().auth.refresh_token_expiry_days));
    t->hashedToken = crypto::hash::password(t->rawToken);

    if (t->hashedToken.empty()) {
        log::Registry::auth()->error("[session::Issuer] Failed to hash refresh token for session {}: {}", session->uuid, "Hashing failed");
        throw std::runtime_error("Failed to hash refresh token");
    }

    session->tokens->refreshToken = t;

    if (session->tokens->refreshToken != t) {
        log::Registry::auth()->error("[session::Issuer] Failed to assign refresh token to session {}: {}", session->uuid, "Assignment failed");
        throw std::runtime_error("Failed to assign refresh token to session");
    }
}

std::optional<TokenClaims> Issuer::decode(const std::string& refreshToken) {
    try {
        const auto decoded = jwt::decode<jwt::traits::nlohmann_json>(refreshToken);

        const auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{runtime::Deps::get().secretsManager->jwtSecret()})
            .with_issuer("Vaulthalla");

        verifier.verify(decoded);

        return TokenClaims {
            .jti = decoded.get_id(),
            .subject = decoded.get_subject(),
            .issuedAt = decoded.get_issued_at(),
            .expiresAt = decoded.get_expires_at()
        };
    } catch (const std::exception& e) {
        log::Registry::auth()->debug("[session::Issuer] Failed to decode refresh token: {}", e.what());
        return std::nullopt;
    }
}

std::string Issuer::buildAccessTokenSubject(const std::shared_ptr<Session>& session) {
    return session->uuid;
}

std::string Issuer::buildRefreshTokenSubject(const std::shared_ptr<Session>& session) {
    return session->ipAddress + ":" + session->userAgent;
}

}
