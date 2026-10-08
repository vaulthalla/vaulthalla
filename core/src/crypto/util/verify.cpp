#include "crypto/util/verify.hpp"
#include "crypto/util/encrypt.hpp"

#include <openssl/evp.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace vh::crypto::util {

namespace {

struct VerifyCipherCtxFree {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};

}

bool aes256_gcm_file_authenticates(const std::filesystem::path& ciphertextPath,
                                   const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& iv) {
    if (key.size() != AES_KEY_SIZE) throw std::invalid_argument("Invalid AES-256 key size");
    if (iv.size() != AES_IV_SIZE) throw std::invalid_argument("Invalid AES-GCM IV size");

    const auto total = std::filesystem::file_size(ciphertextPath);
    if (total < AES_TAG_SIZE) return false;
    const auto bodySize = total - AES_TAG_SIZE;

    std::ifstream in(ciphertextPath, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open ciphertext file: " + ciphertextPath.string());

    std::array<unsigned char, AES_TAG_SIZE> tag{};
    in.seekg(static_cast<std::streamoff>(bodySize));
    in.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    if (!in) throw std::runtime_error("Failed to read AES-GCM tag: " + ciphertextPath.string());
    in.seekg(0);

    const std::unique_ptr<EVP_CIPHER_CTX, VerifyCipherCtxFree> ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::runtime_error("Failed to allocate AES-GCM context");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1)
        throw std::runtime_error("AES-GCM initialization failed");

    std::array<unsigned char, 64 * 1024> input{};
    std::array<unsigned char, input.size() + AES_TAG_SIZE> scratch{};
    const auto wipe = [&] { sodium_memzero(scratch.data(), scratch.size()); };

    uintmax_t remaining = bodySize;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<uintmax_t>(remaining, input.size()));
        in.read(reinterpret_cast<char*>(input.data()), chunk);
        if (in.gcount() != chunk) {
            wipe();
            throw std::runtime_error("Short read verifying: " + ciphertextPath.string());
        }
        remaining -= static_cast<uintmax_t>(chunk);

        int outLen = 0;
        if (EVP_DecryptUpdate(ctx.get(), scratch.data(), &outLen, input.data(), static_cast<int>(chunk)) != 1) {
            wipe();
            return false;
        }
    }
    wipe();

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), tag.data()) != 1)
        throw std::runtime_error("AES-GCM tag setup failed");

    int finalLen = 0;
    const bool ok = EVP_DecryptFinal_ex(ctx.get(), scratch.data(), &finalLen) == 1;
    wipe();
    return ok;
}

}
