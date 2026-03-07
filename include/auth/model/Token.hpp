#pragma once

#include <chrono>
#include <string>

namespace pqxx { class row; }

namespace vh::auth::model {

struct Token {
    std::string rawToken{};
    uint32_t userId{};
    std::time_t expiresAt = 0;
    bool revoked{false};

    virtual ~Token() = default;
    explicit Token(std::string rawToken);
    Token(std::string token, unsigned short userId);
    explicit Token(const pqxx::row& row);

    [[nodiscard]] bool isExpired() const;
    [[nodiscard]] virtual bool isValid() const;
    [[nodiscard]] std::chrono::seconds timeRemaining() const;

    void revoke() { revoked = true; }
};

}
