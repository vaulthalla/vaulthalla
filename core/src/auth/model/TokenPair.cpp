#include "auth/model/TokenPair.hpp"
#include "auth/model/RefreshToken.hpp"

using namespace vh::auth::model;

void TokenPair::revoke() const {
    if (accessToken) accessToken->revoke();
    if (refreshToken) refreshToken->revoke();
}

void TokenPair::invalidate() const {
    if (accessToken) accessToken->revoke();
    if (refreshToken) refreshToken->hardInvalidate();
}

void TokenPair::destroy() {
    invalidate();
    accessToken = nullptr;
    refreshToken = nullptr;
}
