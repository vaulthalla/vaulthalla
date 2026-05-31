#include "protocols/s3/CredentialManager.hpp"

#include "crypto/secrets/TPMKeyProvider.hpp"
#include "crypto/util/encrypt.hpp"
#include "db/query/s3/Gateway.hpp"

#include <algorithm>
#include <array>
#include <paths.h>
#include <sodium.h>
#include <stdexcept>

namespace vh::protocols::s3 {

namespace {
constexpr std::string_view kAccessAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr std::string_view kSecretAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+";

std::string randomFromAlphabet(const std::string_view alphabet, const std::size_t length) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
        out.push_back(alphabet[randombytes_uniform(alphabet.size())]);
    return out;
}
}

CredentialManager::CredentialManager()
    : tpmKeyProvider_(std::make_unique<crypto::secrets::TPMKeyProvider>(
          paths::testMode ? "test_s3gw_master" : "s3gw_master")) {
    tpmKeyProvider_->init();
}

GatewaySecret CredentialManager::createCredential(const uint32_t userId, const std::string& name) const {
    GatewaySecret out;
    out.secret_access_key = generateSecretKey();

    std::vector<uint8_t> iv;
    out.credential.user_id = userId;
    out.credential.name = name;
    out.credential.access_key = generateAccessKey();
    out.credential.encrypted_secret_access_key = encryptSecret(out.secret_access_key, iv);
    out.credential.iv = std::move(iv);
    out.credential.enabled = true;
    out.credential.id = db::query::s3::Gateway::createCredential(out.credential);
    return out;
}

std::vector<db::query::s3::GatewayCredential> CredentialManager::listCredentials(const std::optional<uint32_t> userId) const {
    return db::query::s3::Gateway::listCredentials(userId);
}

bool CredentialManager::revokeCredential(const std::string& accessKeyOrName, const std::optional<uint32_t> userId) const {
    if (accessKeyOrName.starts_with("VH"))
        return db::query::s3::Gateway::deleteCredentialByAccessKey(accessKeyOrName);
    if (!userId) return false;
    return db::query::s3::Gateway::deleteCredentialByName(*userId, accessKeyOrName);
}

std::optional<GatewaySecret> CredentialManager::findEnabledSecret(const std::string& accessKey) const {
    const auto credential = db::query::s3::Gateway::getCredentialByAccessKey(accessKey);
    if (!credential || !credential->enabled) return std::nullopt;
    return GatewaySecret{
        .credential = *credential,
        .secret_access_key = decryptSecret(credential->encrypted_secret_access_key, credential->iv)
    };
}

void CredentialManager::markUsed(const uint32_t credentialId) const {
    db::query::s3::Gateway::updateCredentialLastUsed(credentialId);
}

std::string CredentialManager::generateAccessKey() {
    return "VH" + randomFromAlphabet(kAccessAlphabet, 24);
}

std::string CredentialManager::generateSecretKey() {
    return randomFromAlphabet(kSecretAlphabet, 40);
}

std::vector<uint8_t> CredentialManager::encryptSecret(const std::string& secret, std::vector<uint8_t>& iv) const {
    const auto masterKey = tpmKeyProvider_->getMasterKey();
    const auto plaintext = std::vector<uint8_t>(secret.begin(), secret.end());
    return crypto::util::encrypt_aes256_gcm(plaintext, masterKey, iv);
}

std::string CredentialManager::decryptSecret(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv) const {
    const auto masterKey = tpmKeyProvider_->getMasterKey();
    const auto decrypted = crypto::util::decrypt_aes256_gcm(ciphertext, masterKey, iv);
    return {decrypted.begin(), decrypted.end()};
}

} // namespace vh::protocols::s3
