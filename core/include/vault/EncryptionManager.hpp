#pragma once

#include "crypto/secrets/TPMKeyProvider.hpp"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <filesystem>

namespace vh::fs::model { struct File; }

namespace vh::vault {

class EncryptionManager {
public:
    explicit EncryptionManager(unsigned int vault_id);

    // Must be called before encrypt/decrypt
    void load_key();

    void prepare_key_rotation();
    void finish_key_rotation();

    [[nodiscard]] std::vector<uint8_t>
    rotateDecryptEncrypt(const std::vector<uint8_t>& ciphertext, const std::shared_ptr<fs::model::File>& f) const;

    // Encrypt data with vault key, returns ciphertext.
    // Populates out_b64_iv with base64-encoded IV.
    [[nodiscard]] std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, const std::shared_ptr<fs::model::File>& f) const;

    void encryptFileToFile(const std::filesystem::path& plaintextPath,
                           const std::filesystem::path& ciphertextPath,
                           const std::shared_ptr<fs::model::File>& f) const;

    // Decrypt using base64-encoded IV and ciphertext
    [[nodiscard]] std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext,
                                 const std::string& b64_iv, unsigned int keyVersion) const;

    // Streams a ciphertext file (AES-256-GCM body followed by its 16-byte tag) into a plaintext file. Nothing is
    // left at plaintextPath unless the tag verifies.
    void decryptFileToFile(const std::filesystem::path& ciphertextPath,
                           const std::filesystem::path& plaintextPath,
                           const std::string& b64_iv, unsigned int keyVersion) const;

    [[nodiscard]] std::vector<uint8_t> get_key(const std::string& callingFunctionName) const;

    [[nodiscard]] unsigned int get_key_version() const;

    [[nodiscard]] bool rotation_in_progress() const;

private:
    // The key that decrypts data written under keyVersion (same rules as decrypt()).
    [[nodiscard]] const std::vector<uint8_t>& keyFor(unsigned int keyVersion) const;

    std::unique_ptr<crypto::secrets::TPMKeyProvider> tpmKeyProvider_;
    std::atomic<bool> rotation_in_progress_;
    unsigned int vault_id_, version_{};
    std::vector<uint8_t> key_, old_key_;
};

}
