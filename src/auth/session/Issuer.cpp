#include "auth/session/Issuer.hpp"
#include "auth/model/RefreshToken.hpp"
#include "config/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "crypto/util/hash.hpp"
#include "crypto/secrets/Manager.hpp"
#include "identities/model/User.hpp"
#include "db/query/auth/RefreshToken.hpp"
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
    const auto now = std::chrono::system_clock::now();
    const auto exp = now + ttl;

    t->issuedAt = std::chrono::system_clock::to_time_t(now);
    t->expiresAt = std::chrono::system_clock::to_time_t(exp);
    t->jti = generateUUID();
    t->userId = session->user->id;

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
}

void Issuer::refreshToken(const std::shared_ptr<Session>& session) {
    const auto t = std::make_shared<model::RefreshToken>();
    t->subject = buildRefreshTokenSubject(session);
    t->userAgent = session->userAgent;
    t->ipAddress = session->ipAddress;
    finalizeAndSignToken(session, t, std::chrono::days(vh::config::Registry::get().auth.refresh_token_expiry_days));
    t->hashedToken = crypto::hash::password(t->rawToken);

    db::query::auth::RefreshToken::set(t);
    session->tokens->refreshToken = t;
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
            .expiresAt = decoded.get_expires_at()
        };
    } catch (const std::exception& e) {
        log::Registry::auth()->debug("[AuthManager] Failed to decode refresh token: {}", e.what());
        return std::nullopt;
    }
}

std::string Issuer::buildAccessTokenSubject(const std::shared_ptr<Session>& session) {
    return session->uuid;
}

std::string Issuer::buildRefreshTokenSubject(const std::shared_ptr<Session>& session) {
    return session->ipAddress + ":" + session->userAgent + ":" + session->uuid;
}

}
