#pragma once
#include <memory>

namespace vh::auth::model {

struct Token;
struct RefreshToken;

struct TokenPair {
    std::shared_ptr<Token> accessToken{nullptr};
    std::shared_ptr<RefreshToken> refreshToken{nullptr};

    void revoke() const;
    void invalidate() const;
    void destroy();
};

}
