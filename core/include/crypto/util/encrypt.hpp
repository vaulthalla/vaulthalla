#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>

namespace vh::crypto::util {

constexpr size_t AES_KEY_SIZE = 32;      // 256-bit
constexpr size_t AES_IV_SIZE  = 12;      // GCM standard nonce
constexpr size_t AES_TAG_SIZE = 16;      // GCM auth tag

std::vector<uint8_t> encrypt_aes256_gcm(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    std::vector<uint8_t>& out_iv);

std::vector<uint8_t> decrypt_aes256_gcm(
    const std::vector<uint8_t>& ciphertext_with_tag,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv);

std::vector<uint8_t> read_file(const std::filesystem::path& path);

std::string b64_encode(const std::vector<uint8_t>& data);

std::vector<uint8_t> b64_decode(const std::string& b64);

}
