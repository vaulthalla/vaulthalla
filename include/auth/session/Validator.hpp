#pragma once

#include <memory>
#include <chrono>
#include <string>

namespace vh::protocols::ws { class Session; }

namespace vh::auth::model { struct Token; struct RefreshToken; }

namespace vh::auth::session {

struct TokenClaims;

struct Validator {
    static void validateRefreshToken(const std::shared_ptr<protocols::ws::Session>& session);
    static bool validateAccessToken(const std::shared_ptr<protocols::ws::Session>& session, const std::string& accessToken);

    static bool softValidateSession(const std::shared_ptr<protocols::ws::Session>& session);
    static bool hasUsableAccessToken(const std::shared_ptr<protocols::ws::Session>& session);
    static bool hasUsableRefreshToken(const std::shared_ptr<protocols::ws::Session>& session);

private:
    static void checkForDangerousDiversion(const std::shared_ptr<model::RefreshToken>& incomingToken, const std::shared_ptr<model::RefreshToken>& storedToken);
    static void handlePriorSession(const std::shared_ptr<protocols::ws::Session>& session, const std::shared_ptr<protocols::ws::Session>& priorSession);
    static void validateClaims(const std::shared_ptr<model::Token>& t, const std::optional<TokenClaims>& claims);
};

}
