#pragma once

#include <string>
#include <filesystem>

namespace vh::crypto::hash {

std::string blake2b(const std::filesystem::path& filepath);

// Hashes a password with Argon2id using libsodium
std::string password(const std::string& password);

// Verifies a password against a given Argon2id hash
bool verifyPassword(const std::string& password, const std::string& hash);

std::string generate_secure_password(size_t length = 128);

}
