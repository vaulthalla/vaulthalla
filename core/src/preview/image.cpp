#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include "preview/image.hpp"

#include <stb/stb_image.h>
#include <stb/stb_image_resize.h>
#include <turbojpeg.h>
#include <stdexcept>
#include <fstream>

namespace vh::preview::image {
    void compress_to_jpeg(const uint8_t *rgb_data, const int width, const int height, std::vector<uint8_t> &out_buf,
                          const int quality) {
        tjhandle tj = tjInitCompress();
        if (!tj) throw std::runtime_error("Failed to initialize TurboJPEG compressor");

        unsigned char *jpeg_buf = nullptr;
        unsigned long jpeg_size = 0;

        constexpr int flags = 0; // could use TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE for faster compression at the cost of quality

        if (tjCompress2(
                tj,
                rgb_data,
                width,
                0, // pitch (0 = auto)
                height,
                TJPF_RGB,
                &jpeg_buf,
                &jpeg_size,
                TJSAMP_444, // No chroma subsampling, best quality
                quality,
                flags) != 0) {
            const std::string err = tjGetErrorStr();
            tjDestroy(tj);
            throw std::runtime_error("JPEG compression failed: " + err);
        }

        out_buf.assign(jpeg_buf, jpeg_buf + jpeg_size);
        tjFree(jpeg_buf);
        tjDestroy(tj);
    }

    std::vector<uint8_t> resize_and_compress(
        const std::string &path,
        const std::optional<std::string> &scale_opt,
        const std::optional<std::string> &size_opt) {
        int width, height, channels;
        unsigned char *data = stbi_load(path.c_str(), &width, &height, &channels, 3);
        if (!data) throw std::runtime_error("Failed to load image");

        int new_w = width, new_h = height;
        if (scale_opt) {
            const float scale = std::stof(*scale_opt);
            new_w = static_cast<int>(static_cast<float>(width) * scale);
            new_h = static_cast<int>(static_cast<float>(height) * scale);
        } else if (size_opt) {
            const int max_dim = std::stoi(*size_opt);
            const float ratio = std::min(static_cast<float>(max_dim) / static_cast<float>(width),
                                         static_cast<float>(max_dim) / static_cast<float>(height));
            new_w = static_cast<int>(static_cast<float>(width) * ratio);
            new_h = static_cast<int>(static_cast<float>(height) * ratio);
        }

        std::vector<uint8_t> resized(new_w * new_h * 3);
        stbir_resize_uint8(data, width, height, 0, resized.data(), new_w, new_h, 0, 3);
        stbi_image_free(data);

        // Compress to JPEG using libjpeg-turbo
        std::vector<uint8_t> compressed;
        compress_to_jpeg(resized.data(), new_w, new_h, compressed);
        return compressed;
    }

    std::vector<uint8_t> resize_and_compress_buffer(
        const uint8_t *data, size_t size,
        const std::optional<std::string> &scale_opt,
        const std::optional<std::string> &size_opt) {
        if (size < 4) {
            throw std::runtime_error("Buffer too small to be a valid image");
        }

        int width = 0, height = 0, channels = 0;
        unsigned char *decoded = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 3);
        if (!decoded) {
            const char *reason = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to decode image from memory: ") + (reason ? reason : "unknown error"));
        }

        int new_w = width, new_h = height;
        if (scale_opt) {
            float scale = std::stof(*scale_opt);
            new_w = static_cast<int>(static_cast<float>(width) * scale);
            new_h = static_cast<int>(static_cast<float>(height) * scale);
        } else if (size_opt) {
            int max_dim = std::stoi(*size_opt);
            float ratio = std::min(static_cast<float>(max_dim) / static_cast<float>(width),
                                   static_cast<float>(max_dim) / static_cast<float>(height));
            new_w = static_cast<int>(static_cast<float>(width) * ratio);
            new_h = static_cast<int>(static_cast<float>(height) * ratio);
        }

        std::vector<uint8_t> resized(new_w * new_h * 3);
        stbir_resize_uint8(decoded, width, height, 0, resized.data(), new_w, new_h, 0, 3);
        stbi_image_free(decoded);

        std::vector<uint8_t> compressed;
        compress_to_jpeg(resized.data(), new_w, new_h, compressed);
        return compressed;
    }
}
