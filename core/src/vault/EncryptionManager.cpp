#include "vault/EncryptionManager.hpp"
#include "crypto/util/encrypt.hpp"
#include "log/Registry.hpp"
#include "db/query/vault/Key.hpp"
#include "vault/model/Key.hpp"
#include "fs/model/File.hpp"

#include <sodium.h>
#include <stdexcept>
#include <paths.h>
#include <format>

using namespace vh::vault;
using namespace vh::crypto;
using namespace vh::crypto::util;
using namespace vh::fs::model;

EncryptionManager::EncryptionManager(const unsigned int vault_id)
    : vault_id_(vault_id) {
    tpmKeyProvider_ = std::make_unique<secrets::TPMKeyProvider>(paths::testMode ? "test_vault_master" : "vault_master");
    tpmKeyProvider_->init();
    load_key();
}

unsigned int EncryptionManager::get_key_version() const { return version_; }
bool EncryptionManager::rotation_in_progress() const { return rotation_in_progress_.load(); }

void EncryptionManager::load_key() {
    rotation_in_progress_.store(db::query::vault::Key::keyRotationInProgress(vault_id_));
    const auto rec = db::query::vault::Key::getVaultKey(vault_id_);
    const auto masterKey = tpmKeyProvider_->getMasterKey();

    if (!rec) {
        // First time: generate and seal new vault key
        std::vector<uint8_t> vaultKey(AES_KEY_SIZE);
        randombytes_buf(vaultKey.data(), AES_KEY_SIZE);

        std::vector<uint8_t> iv;
        const auto enc_key = encrypt_aes256_gcm(vaultKey, masterKey, iv);

        const auto key = std::make_shared<vault::model::Key>();
        key->vaultId = vault_id_;
        key->encrypted_key = enc_key;
        key->iv = iv;
        key->version = db::query::vault::Key::addVaultKey(key);
        version_ = key->version;

        key_ = std::move(vaultKey);
        const auto msg = std::format("[VaultEncryptionManager] Created new sealed AES256-GCM key for vault {} with version {}",
                                     vault_id_, version_);
        log::Registry::audit()->info(msg);
        log::Registry::crypto()->info(msg);
        return;
    }

    version_ = rec->version;

    // Decrypt existing vault key
    key_ = decrypt_aes256_gcm(rec->encrypted_key, masterKey, rec->iv);
    if (key_.size() != AES_KEY_SIZE)
        throw std::runtime_error("Vault key must be 32 bytes (AES-256)");

    if (rotation_in_progress_) {
        const auto oldKey = db::query::vault::Key::getRotationInProgressOldKey(vault_id_);
        if (!oldKey) {
            log::Registry::crypto()->error("[VaultEncryptionManager] No old key found for rotation in progress for vault {}",
                                         vault_id_);
            throw std::runtime_error("No old key found for rotation in progress");
        }

        old_key_ = decrypt_aes256_gcm(oldKey->encrypted_key, masterKey, oldKey->iv);
        if (key_.size() != AES_KEY_SIZE) {
            log::Registry::crypto()->error("[VaultEncryptionManager] Old vault key must be 32 bytes (AES-256), got {} bytes",
                                         key_.size());
            throw std::runtime_error("Old vault key must be 32 bytes (AES-256)");
        }

        log::Registry::crypto()->debug("[VaultEncryptionManager] Loaded old key for vault {} during rotation", vault_id_);
    }
}

void EncryptionManager::prepare_key_rotation() {
    if (db::query::vault::Key::keyRotationInProgress(vault_id_)) {
        log::Registry::crypto()->warn("[VaultEncryptionManager] Key rotation already in progress for vault {}", vault_id_);
        return;
    }

    log::Registry::crypto()->debug("[VaultEncryptionManager] Preparing key rotation for vault {}", vault_id_);

    old_key_ = std::move(key_);
    key_.clear();
    key_.resize(AES_KEY_SIZE);
    randombytes_buf(key_.data(), AES_KEY_SIZE);

    std::vector<uint8_t> iv;
    const auto key = std::make_shared<vault::model::Key>();
    key->vaultId = vault_id_;
    key->encrypted_key = encrypt_aes256_gcm(key_, tpmKeyProvider_->getMasterKey(), iv);
    key->iv = std::move(iv);
    key->version = db::query::vault::Key::rotateVaultKey(key);

    version_ = key->version;
    rotation_in_progress_.store(true);

    const auto msg = fmt::format("[VaultEncryptionManager] Prepared key rotation for vault {} with new version {}",
                                         vault_id_, version_);
    log::Registry::audit()->info(msg);
    log::Registry::crypto()->info(msg);
}

void EncryptionManager::finish_key_rotation() {
    if (!db::query::vault::Key::keyRotationInProgress(vault_id_)) {
        log::Registry::crypto()->warn("[VaultEncryptionManager] No key rotation in progress for vault {}", vault_id_);
        return;
    }

    old_key_.clear();

    db::query::vault::Key::markKeyRotationFinished(vault_id_);
    rotation_in_progress_.store(false);

    const auto msg = fmt::format("[VaultEncryptionManager] Finished key rotation for vault {} with version {}",
                                         vault_id_, version_);
    log::Registry::audit()->info(msg);
    log::Registry::crypto()->info(msg);
}

std::vector<uint8_t> EncryptionManager::rotateDecryptEncrypt(const std::vector<uint8_t>& ciphertext, const std::shared_ptr<File>& f) const {
    try {
        if (f->encrypted_with_key_version == version_) {
            log::Registry::crypto()->debug("[VaultEncryptionManager] Key version {} is current for vault {}, no rotation needed",
                                         f->encrypted_with_key_version, vault_id_);
            return ciphertext;
        }

        if (!rotation_in_progress_.load()) {
            const auto msg = fmt::format(
                "[VaultEncryptionManager] Key rotation not in progress for vault {}, but key version {} is not current",
                vault_id_, f->encrypted_with_key_version);
            log::Registry::audit()->warn(msg);
            log::Registry::crypto()->warn(msg);
            throw std::runtime_error("Key rotation not in progress, cannot rotate key");
        }

        if (f->encrypted_with_key_version != version_ - 1)
            log::Registry::crypto()->warn("[VaultEncryptionManager] Key version {} is not the previous version {}, using new key",
                                        f->encrypted_with_key_version, version_);

        const auto decrypted = decrypt_aes256_gcm(ciphertext, old_key_, b64_decode(f->encryption_iv));

        std::vector<uint8_t> iv;
        const auto encrypted = encrypt_aes256_gcm(decrypted, key_, iv);

        if (encrypted.size() != ciphertext.size()) {
            log::Registry::crypto()->error("[VaultEncryptionManager] Encrypted data size mismatch after key rotation");
            throw std::runtime_error("Encrypted data size mismatch after key rotation");
        }

        f->encryption_iv = b64_encode(iv);
        f->encrypted_with_key_version = version_;

        return encrypted;
    } catch (const std::exception& e) {
        log::Registry::crypto()->error("[VaultEncryptionManager] Exception during key rotation: {}", e.what());
        throw std::runtime_error("Key rotation failed: " + std::string(e.what()));
    }
}

std::vector<uint8_t> EncryptionManager::encrypt(const std::vector<uint8_t>& plaintext, const std::shared_ptr<File>& f) const {
    std::vector<uint8_t> iv;

    auto ciphertext = encrypt_aes256_gcm(plaintext, key_, iv);
    f->encryption_iv = b64_encode(iv);
    f->encrypted_with_key_version = version_;
    return ciphertext;
}

std::vector<uint8_t> EncryptionManager::decrypt(const std::vector<uint8_t>& ciphertext, const std::string& b64_iv, const unsigned int keyVersion) const {
    if (rotation_in_progress_.load()) {
        if (key_.empty() || old_key_.empty()) throw std::runtime_error("Key rotation in progress but keys are not set");

        if (keyVersion == version_) return decrypt_aes256_gcm(ciphertext, key_, b64_decode(b64_iv));
        if (keyVersion == version_ - 1) return decrypt_aes256_gcm(ciphertext, old_key_, b64_decode(b64_iv));

        if (keyVersion < version_ - 1)
            log::Registry::crypto()->warn("[VaultEncryptionManager] Key version {} is too old for vault {}, using new key",
                                        keyVersion, vault_id_);
        else if (keyVersion > version_)
            log::Registry::crypto()->warn("[VaultEncryptionManager] Key version {} is newer than current version {} for vault {}, using new key",
                                        keyVersion, version_, vault_id_);

        return decrypt_aes256_gcm(ciphertext, key_, b64_decode(b64_iv));
    }

    if (keyVersion != version_) {
        log::Registry::crypto()->warn("[VaultEncryptionManager] Key version mismatch: expected {}, got {} for vault {}",
                                    version_, keyVersion, vault_id_);
        throw std::runtime_error("Key version mismatch");
    }

    return decrypt_aes256_gcm(ciphertext, key_, b64_decode(b64_iv));
}

std::vector<uint8_t> EncryptionManager::get_key(const std::string& callingFunctionName) const {
    if (key_.empty()) {
        log::Registry::crypto()->error("[VaultEncryptionManager] Key is empty in function: {}", callingFunctionName);
        throw std::runtime_error("Vault key is not initialized");
    }

    const auto msg = fmt::format("[VaultEncryptionManager] Returning key for vault {} in function: {}",
                           vault_id_, callingFunctionName);
    log::Registry::crypto()->debug(msg);
    log::Registry::audit()->debug(msg);

    return key_;
}

