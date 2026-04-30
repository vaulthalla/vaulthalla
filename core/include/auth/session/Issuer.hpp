#pragma once

#include <memory>
#include <optional>
#include <chrono>
#include <string>
#include <string_view>

namespace vh::auth::model { struct Token; struct RefreshToken; }
namespace vh::protocols::ws { class Session; }

namespace vh::auth::session {

struct TokenClaims {
    std::string jti, subject;
    std::string tokenKind;
    std::chrono::system_clock::time_point issuedAt, expiresAt;
};

struct Issuer {
    static void accessToken(const std::shared_ptr<protocols::ws::Session>& session);
    static void refreshToken(const std::shared_ptr<protocols::ws::Session>& session);
    static void shareRefreshToken(const std::shared_ptr<protocols::ws::Session>& session);
    static std::optional<TokenClaims> decode(const std::string& refreshToken);

    static constexpr std::string_view accessTokenKind() noexcept { return "access"; }
    static constexpr std::string_view refreshTokenKind() noexcept { return "refresh"; }
    static constexpr std::string_view shareRefreshTokenKind() noexcept { return "share_refresh"; }

    static std::string buildAccessTokenSubject(const std::shared_ptr<protocols::ws::Session>& session);
    static std::string buildRefreshTokenSubject(const std::shared_ptr<protocols::ws::Session>& session);

    static void setJwtSecretForTesting(std::string secret);
    static void clearJwtSecretForTesting();
};

}
