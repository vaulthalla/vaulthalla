#pragma once

#include "auth/model/Token.hpp"

#include <cstdint>
#include <ctime>
#include <string>

namespace pqxx { class row; }

namespace vh::auth::model {

struct RefreshToken final : Token {
    std::string jti, hashedToken, userAgent, ipAddress;
    std::time_t createdAt = 0, lastUsed = 0;

    RefreshToken(std::string jti,
                 std::string rawToken,
                 std::string hashedToken,
                 uint32_t userId,
                 std::string userAgent,
                 std::string ipAddress);

    explicit RefreshToken(std::string rawToken);

    explicit RefreshToken(const pqxx::row& row);

    [[nodiscard]] bool isValid() const override;

    static std::shared_ptr<RefreshToken> fromIssuedToken(
        std::string jti,
        std::string rawToken,
        std::string hashedToken,
        std::uint32_t userId,
        std::string userAgent,
        std::string ipAddress
    );
};

}
