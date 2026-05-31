#pragma once

#include "crypto/secrets/TPMKeyProvider.hpp"
#include "db/query/s3/Gateway.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::identities { struct User; }
namespace vh::protocols::s3 {

struct GatewaySecret {
    db::query::s3::GatewayCredential credential;
    std::string secret_access_key;
};

class CredentialManager {
public:
    CredentialManager();

    GatewaySecret createCredential(uint32_t userId, const std::string& name) const;
    std::vector<db::query::s3::GatewayCredential> listCredentials(std::optional<uint32_t> userId = std::nullopt) const;
    bool revokeCredential(const std::string& accessKeyOrName, std::optional<uint32_t> userId = std::nullopt) const;

    std::optional<GatewaySecret> findEnabledSecret(const std::string& accessKey) const;
    void markUsed(uint32_t credentialId) const;

    static std::string generateAccessKey();
    static std::string generateSecretKey();

private:
    std::unique_ptr<crypto::secrets::TPMKeyProvider> tpmKeyProvider_;

    [[nodiscard]] std::vector<uint8_t> encryptSecret(const std::string& secret, std::vector<uint8_t>& iv) const;
    [[nodiscard]] std::string decryptSecret(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv) const;
};

}
