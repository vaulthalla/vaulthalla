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

    static bool tryRehydrateFromPriorSession(const std::shared_ptr<protocols::ws::Session>& session, const std::string& rawToken, const std::optional<TokenClaims>& claims);
    static void rehydrateFromStoredRefreshToken(const std::shared_ptr<protocols::ws::Session>& session, const std::string& rawToken, const std::optional<TokenClaims>& claims);

    static bool softValidateActiveSession(const std::shared_ptr<protocols::ws::Session>& session);
    static bool hasUsableAccessToken(const std::shared_ptr<protocols::ws::Session>& session);
    static bool hasUsableRefreshToken(const std::shared_ptr<protocols::ws::Session>& session);

    static void checkForDangerousDiversion(const std::shared_ptr<model::RefreshToken>& incomingToken, const std::shared_ptr<model::RefreshToken>& storedToken);
    static bool hasUsableRefreshContext(const std::shared_ptr<protocols::ws::Session>& session);

    static void validateClaims(const std::shared_ptr<model::Token>& t, const std::optional<TokenClaims>& claims);
    static void validateRefreshClaims(const std::shared_ptr<protocols::ws::Session>& session, const std::optional<TokenClaims>& claims);
};

}
