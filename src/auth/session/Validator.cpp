#include "auth/session/Validator.hpp"
#include "auth/model/RefreshToken.hpp"
#include "protocols/ws/Session.hpp"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

using namespace vh::auth::session;

bool Validator::validateAccessToken(const std::shared_ptr<protocols::ws::Session>& session,
                                    const std::string& accessToken) {
    try {
        const auto decoded = jwt::decode<jwt::traits::nlohmann_json>(accessToken);

        const auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{session->tokens.})
            .with_issuer("vaulthalla");

        verifier.verify(decoded);

        return true;
    } catch (const std::exception& e) {
        log::Registry::ws()->debug("[Client] Token validation failed: {}", e.what());
        return false;
    }
}


bool Validator::validateSession(const std::shared_ptr<protocols::ws::Session>& session) {
    return session->user &&
        session->tokens && session->tokens->isValid() &&
        session->refreshToken && session->refreshToken->isValid();
}