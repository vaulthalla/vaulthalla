#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Compatibility entry point for the legacy ws share preview lane (first page), over preview::render.
namespace vh::preview::pdf {
    std::vector<uint8_t> resize_and_compress_buffer(
        const uint8_t *data, size_t size,
        const std::optional<std::string> &scale,
        const std::optional<std::string> &max_size);
}
