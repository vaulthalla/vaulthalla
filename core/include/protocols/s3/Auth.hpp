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
    uint32_t credential_id{};
    std::string access_key;
    std::string scope_mode{"user_access"};
    bool enforce_budget_for_local_requests{false};
    bool dev_context{false};
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
