#pragma once
#include <memory>
#include "auth/Fwd.hpp"

namespace vh::auth::model {

struct TokenPair {
    std::shared_ptr<Token> accessToken{nullptr};
    std::shared_ptr<RefreshToken> refreshToken{nullptr};
    std::shared_ptr<RefreshToken> shareRefreshToken{nullptr};

    void revoke() const;
    void invalidate() const;
    void destroy();
};

}
