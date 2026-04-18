#include "crypto/secrets/Manager.hpp"
#include "db/query/crypto/Secret.hpp"
#include "crypto/model/Secret.hpp"
#include "crypto/util/encrypt.hpp"
#include "crypto/util/hash.hpp"

#include <paths.h>

using namespace vh::crypto::util;
using namespace vh::crypto::model;

namespace vh::crypto::secrets {

Manager::Manager()
: tpmKeyProvider_(std::make_unique<TPMKeyProvider>(paths::testMode ? "test_master" : "master")) {
    tpmKeyProvider_->init();
}

std::string Manager::jwtSecret() const {
    return getOrInitSecret("jwt_secret");
}

void Manager::setJWTSecret(const std::string& secret) const {
    return setEncryptedValue("jwt_secret", secret);
}

std::string Manager::getOrInitSecret(const std::string& key) const {
    const auto secret = db::query::crypto::Secret::getSecret(key);
    if (!secret) {
        const auto newSecret = hash::generate_secure_password(64);
        setEncryptedValue(key, newSecret);
        return newSecret;
    }

    {
        std::scoped_lock lock(mutex_);
        const auto masterKey = tpmKeyProvider_->getMasterKey();
        const auto decrypted = decrypt_aes256_gcm(secret->value, masterKey, secret->iv);
        return {decrypted.begin(), decrypted.end()};
    }
}

void Manager::setEncryptedValue(const std::string& key, const std::string& value) const {
    std::scoped_lock lock(mutex_);

    const auto masterKey = tpmKeyProvider_->getMasterKey();
    std::vector<uint8_t> iv;
    const auto plaintext = std::vector<uint8_t>(value.begin(), value.end());
    const auto ciphertext = encrypt_aes256_gcm(plaintext, masterKey, iv);

    const auto secret = std::make_shared<Secret>();
    secret->key = key;
    secret->value = ciphertext;
    secret->iv = iv;

    db::query::crypto::Secret::upsertSecret(secret);
}

}
