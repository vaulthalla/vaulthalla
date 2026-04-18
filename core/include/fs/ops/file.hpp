#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <random>

namespace vh::storage {
struct Engine;
}

namespace vh::fs::ops {

std::vector<uint8_t> readFileToVector(const std::filesystem::path& path);

std::string readFileToString(const std::filesystem::path& path);

void writeFile(const std::filesystem::path& absPath, const std::vector<uint8_t>& ciphertext);

std::string generate_random_suffix(size_t length = 8);

std::filesystem::path decrypt_file_to_temp(unsigned int vault_id,
                                           const std::filesystem::path& rel_path,
                                           const std::shared_ptr<storage::Engine>& engine);

bool isProbablyEncrypted(const std::filesystem::path& path);

std::string bytesToSize(uintmax_t bytes);

}
