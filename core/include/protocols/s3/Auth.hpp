#pragma once

#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/SigV4.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace vh::identities { struct User; }

namespace vh::protocols::s3 {

struct AuthContext {
    std::shared_ptr<identities::User> user;
    db::query::s3::GatewayCredential credential;
};

class Authenticator {
public:
    explicit Authenticator(bool requireSigV4 = true);

    AuthContext authenticate(const sigv4::VerificationInput& input) const;

private:
    bool requireSigV4_{true};
    mutable std::mutex credentialsMutex_;
    mutable std::unique_ptr<CredentialManager> credentials_;

    [[nodiscard]] CredentialManager& credentials() const;
};

}
