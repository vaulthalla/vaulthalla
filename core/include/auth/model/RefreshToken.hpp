#pragma once

#include "auth/model/Token.hpp"

namespace pqxx { class row; }

namespace vh::protocols::ws { class Session; }

namespace vh::auth::model {

struct RefreshToken final : Token {
    std::string hashedToken, userAgent, ipAddress;
    std::chrono::system_clock::time_point lastUsed = std::chrono::system_clock::now();

    ~RefreshToken() override = default;
    RefreshToken() = default;
    explicit RefreshToken(std::string rawToken);
    explicit RefreshToken(const pqxx::row& row);

    [[nodiscard]] bool isValid() const override;

    void hardInvalidate();

    [[nodiscard]] bool dangerousDivergence(const std::shared_ptr<RefreshToken>& other) const;

    [[nodiscard]] Type type() const override { return Type::Refresh; }

    static void addToSession(const std::shared_ptr<protocols::ws::Session>& session, std::string token);
};

bool operator==(const std::shared_ptr<RefreshToken>& lhs, const std::shared_ptr<RefreshToken>& rhs);

}
