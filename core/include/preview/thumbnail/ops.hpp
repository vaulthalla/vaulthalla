#pragma once

#include <vector>
#include <string>
#include <filesystem>

namespace vh::preview::thumbnail {
    void generateAndStore(const std::vector<uint8_t> &buffer, const std::filesystem::path &outputPath,
                          const std::string &mime, unsigned int size = 128);
}