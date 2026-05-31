#include "protocols/s3/Auth.hpp"

#include "db/query/identities/User.hpp"
#include "protocols/s3/Error.hpp"

#include <algorithm>
#include <cctype>

namespace vh::protocols::s3 {

namespace {
bool hasSecurityTokenHeader(const sigv4::VerificationInput& input) {
    for (const auto& [name, value] : input.headers) {
        auto normalized = name;
        std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (normalized == "x-amz-security-token" && !value.empty()) return true;
    }
    return false;
}
}

Authenticator::Authenticator(const bool requireSigV4)
    : requireSigV4_(requireSigV4) {}

CredentialManager& Authenticator::credentials() const {
    std::scoped_lock lock(credentialsMutex_);
    if (!credentials_)
        credentials_ = std::make_unique<CredentialManager>();
    return *credentials_;
}

AuthContext Authenticator::authenticate(const sigv4::VerificationInput& input) const {
    if (!requireSigV4_) {
        auto user = db::query::identities::User::getUserById(1);
        if (!user) throw accessDenied("/");
        return {.user = std::move(user), .credential = {}};
    }

    std::string parseError;
    const auto parsed = sigv4::parseAuthorization(input, parseError);
    if (!parsed) throw S3Error{"SignatureDoesNotMatch", parseError, http::status::forbidden, input.target};

    if (input.target.find("X-Amz-Security-Token=") != std::string::npos || hasSecurityTokenHeader(input))
        throw S3Error{"InvalidToken", "Temporary security tokens are not supported", http::status::bad_request, input.target};

    auto& credentialManager = credentials();
    const auto secret = credentialManager.findEnabledSecret(parsed->credential.access_key);
    if (!secret) throw accessDenied(input.target);

    const auto result = sigv4::verify(input, secret->secret_access_key);
    if (!result.ok) throw S3Error{"SignatureDoesNotMatch", result.error, http::status::forbidden, input.target};

    auto user = db::query::identities::User::getUserById(secret->credential.user_id);
    if (!user || !user->meta.is_active) throw accessDenied(input.target);

    credentialManager.markUsed(secret->credential.id);
    return {
        .user = std::move(user),
        .credential = secret->credential
    };
}

} // namespace vh::protocols::s3
