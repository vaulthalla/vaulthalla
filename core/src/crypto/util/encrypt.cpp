#include "crypto/util/encrypt.hpp"
#include "log/Registry.hpp"
#include "config/Registry.hpp"

#include <sodium.h>
#include <stdexcept>
#include <fstream>
#include <cstring>

using namespace vh::config;

namespace vh::crypto::util {

static bool is_aes_gcm_supported() {
    if (Registry::get().dev.enabled || std::getenv("VH_ALLOW_FAKE_AES")) return true;
    return crypto_aead_aes256gcm_is_available() != 0;
}

std::vector<uint8_t> encrypt_aes256_gcm(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    std::vector<uint8_t>& out_iv)
{
    if (key.size() != AES_KEY_SIZE) {
        log::Registry::crypto()->error("[encrypt_aes256_gcm] Invalid AES-256 key size: {} bytes", key.size());
        throw std::invalid_argument("Invalid AES-256 key size");
    }

    out_iv.resize(AES_IV_SIZE);
    randombytes_buf(out_iv.data(), AES_IV_SIZE);

    std::vector<uint8_t> ciphertext(plaintext.size() + AES_TAG_SIZE);

    unsigned long long ciphertext_len = 0;
    if (!is_aes_gcm_supported())
        throw std::runtime_error("AES256-GCM not supported on this CPU");

    crypto_aead_aes256gcm_encrypt(
        ciphertext.data(), &ciphertext_len,
        plaintext.data(), plaintext.size(),
        nullptr, 0,  // no AAD
        nullptr, out_iv.data(), key.data());

    ciphertext.resize(ciphertext_len);
    return ciphertext;
}

std::vector<uint8_t> decrypt_aes256_gcm(
    const std::vector<uint8_t>& ciphertext_with_tag,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv)
{
    if (key.size() != AES_KEY_SIZE || iv.size() != AES_IV_SIZE) {
        log::Registry::crypto()->error("[decrypt_aes256_gcm] Invalid key or IV size: "
                                     "key size = {}, iv size = {}",
                                     key.size(), iv.size());
        throw std::invalid_argument("Invalid key or IV size");
    }

    std::vector<uint8_t> decrypted(ciphertext_with_tag.size() - AES_TAG_SIZE);
    unsigned long long decrypted_len = 0;

    if (!is_aes_gcm_supported())
        throw std::runtime_error("AES256-GCM not supported on this CPU");

    if (crypto_aead_aes256gcm_decrypt(
            decrypted.data(), &decrypted_len,
            nullptr,
            ciphertext_with_tag.data(), ciphertext_with_tag.size(),
            nullptr, 0,  // no AAD
            iv.data(), key.data()) != 0)
    {
        throw std::runtime_error("Decryption failed: authentication error");
    }

    decrypted.resize(decrypted_len);
    return decrypted;
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open vault key file: " + path.string());

    std::vector<uint8_t> buf(std::istreambuf_iterator<char>(in), {});
    return buf;
}

std::string b64_encode(const std::vector<uint8_t>& data) {
    const size_t encoded_len = sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string result(encoded_len, '\0');

    sodium_bin2base64(result.data(), result.size(),
                      data.data(), data.size(),
                      sodium_base64_VARIANT_ORIGINAL);

    result.resize(std::strlen(result.c_str())); // Trim null terminator
    return result;
}

std::vector<uint8_t> b64_decode(const std::string& b64) {
    std::vector<uint8_t> decoded(AES_IV_SIZE);
    size_t out_len = 0;
    if (sodium_base642bin(decoded.data(), decoded.size(),
                          b64.c_str(), b64.size(),
                          nullptr, &out_len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
    {
        throw std::runtime_error("Invalid base64 IV");
    }
    decoded.resize(out_len);
    return decoded;
}

}
