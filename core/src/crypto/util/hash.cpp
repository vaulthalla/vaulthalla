#include "crypto/util/hash.hpp"

#include <sodium.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

constexpr std::size_t OPSLIMIT = crypto_pwhash_OPSLIMIT_MODERATE;
constexpr std::size_t MEMLIMIT = crypto_pwhash_MEMLIMIT_MODERATE;

namespace vh::crypto::hash {
std::string blake2b(const std::filesystem::path& filepath) {
    constexpr size_t hash_len = crypto_generichash_BYTES;
    unsigned char hash[hash_len];

    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open file for hashing: " + filepath.string());

    crypto_generichash_state state;
    crypto_generichash_init(&state, nullptr, 0, hash_len);

    char buffer[8192];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        crypto_generichash_update(&state, reinterpret_cast<unsigned char*>(buffer), file.gcount());
    }

    crypto_generichash_final(&state, hash, hash_len);

    std::ostringstream result;
    for (size_t i = 0; i < hash_len; ++i)
        result << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    return result.str();
}

std::string password(const std::string& password) {
    char hashed[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str(hashed, password.c_str(), password.size(), OPSLIMIT, MEMLIMIT) != 0)
        throw std::runtime_error("Password hashing failed (out of memory?)");

    return {hashed};
}

bool verifyPassword(const std::string& password, const std::string& hash) {
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0;
}

std::string generate_secure_password(const size_t length) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium failed to initialize");

    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "!@#$%^&*()-_=+[]{}<>?/|\\:;,.~";

    std::string password;
    password.reserve(length);
    for (size_t i = 0; i < length; ++i) password.push_back(alphabet[randombytes_uniform(alphabet.size())]);
    return password;
}

}
