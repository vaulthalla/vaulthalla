#pragma once

#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <filesystem>

namespace vh::preview::pdf {
    std::vector<uint8_t> resize_and_compress_buffer(
        const uint8_t *data, size_t size,
        const std::optional<std::string> &scale,
        const std::optional<std::string> &max_size);
}