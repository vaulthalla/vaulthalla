#pragma once

#include <chrono>
#include <string>
#include <optional>
#include <memory>

namespace pqxx { class row; }
namespace vh::protocols::ws { class Session; }
namespace vh::auth::session { struct TokenClaims; }

namespace vh::auth::model {

struct Token {
    enum class Type {
        Access,
        Refresh
    };

    std::string rawToken{};
    std::string jti{};
    std::string subject{};
    uint32_t userId{};
    std::chrono::system_clock::time_point issuedAt = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point expiresAt = std::chrono::system_clock::now();
    bool revoked{false};

    virtual ~Token() = default;
    Token() = default;
    explicit Token(std::string rawToken);
    explicit Token(const pqxx::row& row);

    [[nodiscard]] bool isExpired() const;
    [[nodiscard]] virtual bool isValid() const;
    [[nodiscard]] std::chrono::seconds timeRemaining() const;

    void revoke() { revoked = true; }

    [[nodiscard]] virtual Type type() const { return Type::Access; }
    virtual void dangerousDivergence(const std::optional<session::TokenClaims>& claims) const;
};

bool operator==(const std::shared_ptr<Token>& lhs, const std::shared_ptr<Token>& rhs);

}
