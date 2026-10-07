#include "preview/image.hpp"
#include "preview/pdf.hpp"
#include "preview/render/Raster.hpp"

#include <algorithm>

namespace vh::preview {

namespace {
[[nodiscard]] uint32_t boundedSize(const std::optional<std::string>& maxSize) {
    uint32_t size = 512;
    if (maxSize) {
        try {
            size = static_cast<uint32_t>(std::stoul(*maxSize));
        } catch (...) {
            size = 512;
        }
    }
    return std::clamp<uint32_t>(size, 16, 2048);
}
}

std::vector<uint8_t> image::resize_and_compress_buffer(const uint8_t* data, const size_t size,
                                                       const std::optional<std::string>&,
                                                       const std::optional<std::string>& maxSize) {
    const auto target = boundedSize(maxSize);
    const auto limits = render::limitsFromConfig();
    return render::encodeJpeg(render::fit(render::decodeImage({data, size}, target, limits), target));
}

std::vector<uint8_t> pdf::resize_and_compress_buffer(const uint8_t* data, const size_t size,
                                                     const std::optional<std::string>&,
                                                     const std::optional<std::string>& maxSize) {
    const auto target = boundedSize(maxSize);
    uint32_t pages = 0;
    return render::encodeJpeg(render::renderPdfPage({data, size}, 0, target, render::limitsFromConfig(), pages));
}

}
