#include "crypto/secrets/Manager.hpp"
#include "db/query/crypto/Secret.hpp"
#include "crypto/model/Secret.hpp"
#include "crypto/util/encrypt.hpp"
#include "crypto/util/hash.hpp"

#include <paths.h>
#include <utility>

using namespace vh::crypto::util;
using namespace vh::crypto::model;

namespace vh::crypto::secrets {

Manager::Manager()
: tpmKeyProvider_(std::make_unique<TPMKeyProvider>(paths::testMode ? "test_master" : "master")) {
    tpmKeyProvider_->init();
}

Manager::Manager(std::unordered_map<std::string, std::string> testSecrets)
: testSecrets_(std::move(testSecrets)) {}

std::shared_ptr<Manager> Manager::createForTesting(std::unordered_map<std::string, std::string> secrets) {
    return std::shared_ptr<Manager>(new Manager(std::move(secrets)));
}

std::string Manager::jwtSecret() const {
    return getOrInitSecret("jwt_secret");
}

void Manager::setJWTSecret(const std::string& secret) const {
    return setEncryptedValue("jwt_secret", secret);
}

std::optional<std::string> Manager::getSecret(const std::string& key) const {
    if (testSecrets_) {
        std::scoped_lock lock(mutex_);
        const auto it = testSecrets_->find(key);
        if (it == testSecrets_->end()) return std::nullopt;
        return it->second;
    }

    const auto secret = db::query::crypto::Secret::getSecret(key);
    if (!secret) return std::nullopt;
    return decryptStoredSecret(secret->value, secret->iv);
}

void Manager::setSecret(const std::string& key, const std::string& value) const {
    if (testSecrets_) {
        std::scoped_lock lock(mutex_);
        (*testSecrets_)[key] = value;
        return;
    }

    setEncryptedValue(key, value);
}

bool Manager::hasSecret(const std::string& key) const {
    if (testSecrets_) {
        std::scoped_lock lock(mutex_);
        return testSecrets_->contains(key);
    }
    return db::query::crypto::Secret::secretExists(key);
}

std::string Manager::getOrInitSecret(const std::string& key) const {
    if (testSecrets_) {
        std::scoped_lock lock(mutex_);
        if (const auto it = testSecrets_->find(key); it != testSecrets_->end())
            return it->second;

        const auto newSecret = hash::generate_secure_password(64);
        (*testSecrets_)[key] = newSecret;
        return newSecret;
    }

    const auto secret = db::query::crypto::Secret::getSecret(key);
    if (!secret) {
        const auto newSecret = hash::generate_secure_password(64);
        setEncryptedValue(key, newSecret);
        return newSecret;
    }

    return decryptStoredSecret(secret->value, secret->iv);
}

std::string Manager::decryptStoredSecret(const std::vector<uint8_t>& value, const std::vector<uint8_t>& iv) const {
    std::scoped_lock lock(mutex_);
    const auto masterKey = tpmKeyProvider_->getMasterKey();
    const auto decrypted = decrypt_aes256_gcm(value, masterKey, iv);
    return {decrypted.begin(), decrypted.end()};
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
