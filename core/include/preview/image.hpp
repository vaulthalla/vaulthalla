#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Compatibility entry points for the legacy ws share preview lane, implemented over preview::render (bounded,
// in-memory). `scale` is ignored: the output is fit into `max_size` (default 512, capped at 2048).
namespace vh::preview::image {
    std::vector<uint8_t> resize_and_compress_buffer(
        const uint8_t *data, size_t size,
        const std::optional<std::string> &scale,
        const std::optional<std::string> &max_size);
}
