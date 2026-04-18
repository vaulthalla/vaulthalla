#pragma once

#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <filesystem>

namespace vh::preview::image {
    void compress_to_jpeg(const uint8_t *rgb_data, int width, int height, std::vector<uint8_t> &out_buf,
                          int quality = 85);

    std::vector<uint8_t> resize_and_compress(const std::string &path,
                                             const std::optional<std::string> &scale_opt,
                                             const std::optional<std::string> &size_opt);

    std::vector<uint8_t> resize_and_compress_buffer(
        const uint8_t *data, size_t size,
        const std::optional<std::string> &scale,
        const std::optional<std::string> &max_size);
}