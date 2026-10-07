#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "preview/render/Raster.hpp"

#include "config/Registry.hpp"

#include <stb/stb_image.h>
#include <stb/stb_image_resize.h>
#include <turbojpeg.h>
#include <pdfium/fpdfview.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace vh::preview::render {

namespace {

struct TjDeleter {
    void operator()(void* handle) const noexcept {
        if (handle) tjDestroy(handle);
    }
};
using TjHandle = std::unique_ptr<void, TjDeleter>;

[[nodiscard]] bool looksLikeJpeg(const std::span<const uint8_t> bytes) {
    return bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

[[nodiscard]] uint64_t pixels(const uint64_t w, const uint64_t h) { return w * h; }

void checkSource(const std::span<const uint8_t> bytes, const Limits& limits) {
    if (bytes.empty()) throw InvalidInput("Empty image");
    if (bytes.size() > limits.maxSourceBytes) throw LimitExceeded("Source exceeds the preview size limit");
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw LimitExceeded("Source too large to decode");
}

[[nodiscard]] Raster decodeJpeg(const std::span<const uint8_t> bytes, const uint32_t targetMaxDim, const Limits& limits) {
    TjHandle tj(tjInitDecompress());
    if (!tj) throw std::runtime_error("TurboJPEG unavailable");
    int width = 0, height = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(tj.get(), bytes.data(), static_cast<unsigned long>(bytes.size()), &width, &height, &subsamp,
                            &colorspace) != 0 || width <= 0 || height <= 0)
        throw InvalidInput("Invalid JPEG header");

    // Smallest DCT scale whose longest edge still covers the target (so the final resize only ever shrinks).
    int count = 0;
    const tjscalingfactor* factors = tjGetScalingFactors(&count);
    int outW = width, outH = height;
    const auto longest = static_cast<uint32_t>(std::max(width, height));
    for (int i = 0; factors && i < count; ++i) {
        const int sw = TJSCALED(width, factors[i]);
        const int sh = TJSCALED(height, factors[i]);
        if (static_cast<uint32_t>(std::max(sw, sh)) >= std::min(targetMaxDim, longest) &&
            pixels(sw, sh) < pixels(outW, outH)) {
            outW = sw;
            outH = sh;
        }
    }
    if (pixels(outW, outH) > limits.maxPixels) throw LimitExceeded("Image exceeds the pixel limit");

    Raster r;
    r.width = static_cast<uint32_t>(outW);
    r.height = static_cast<uint32_t>(outH);
    r.rgb.resize(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 3);
    if (tjDecompress2(tj.get(), bytes.data(), static_cast<unsigned long>(bytes.size()), r.rgb.data(), outW, 0, outH,
                      TJPF_RGB, TJFLAG_ACCURATEDCT) != 0 &&
        tjGetErrorCode(tj.get()) == TJERR_FATAL)
        throw InvalidInput(std::string("JPEG decode failed: ") + tjGetErrorStr2(tj.get()));
    return r;
}

[[nodiscard]] Raster decodeStb(const std::span<const uint8_t> bytes, const Limits& limits) {
    int width = 0, height = 0, channels = 0;
    const auto len = static_cast<int>(bytes.size());
    if (!stbi_info_from_memory(bytes.data(), len, &width, &height, &channels) || width <= 0 || height <= 0)
        throw InvalidInput("Unsupported or invalid image");
    if (pixels(width, height) > limits.maxPixels) throw LimitExceeded("Image exceeds the pixel limit");

    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(
        stbi_load_from_memory(bytes.data(), len, &width, &height, &channels, 3), &stbi_image_free);
    if (!data) throw InvalidInput(std::string("Image decode failed: ") + stbi_failure_reason());
    Raster r;
    r.width = static_cast<uint32_t>(width);
    r.height = static_cast<uint32_t>(height);
    r.rgb.assign(data.get(), data.get() + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    return r;
}

}

Limits limitsFromConfig() {
    const auto& cfg = config::Registry::get();
    Limits limits;
    limits.maxSourceBytes = std::max<uint64_t>(1, cfg.http_preview.max_preview_size_bytes);
    limits.maxPixels = std::max<uint64_t>(1, cfg.preview.max_render_pixels);
    return limits;
}

std::mutex& pdfiumMutex() {
    static std::mutex mutex;
    return mutex;
}

Raster decodeImage(const std::span<const uint8_t> bytes, const uint32_t targetMaxDim, const Limits& limits) {
    checkSource(bytes, limits);
    return looksLikeJpeg(bytes) ? decodeJpeg(bytes, std::max<uint32_t>(1, targetMaxDim), limits) : decodeStb(bytes, limits);
}

Raster renderPdfPage(const std::span<const uint8_t> bytes, const uint32_t page, const uint32_t targetMaxDim,
                     const Limits& limits, uint32_t& pageCount) {
    checkSource(bytes, limits);
    const auto target = std::clamp<uint32_t>(targetMaxDim, 16, limits.maxDimension);

    std::scoped_lock lock(pdfiumMutex());
    struct Doc {
        FPDF_DOCUMENT d;
        ~Doc() { if (d) FPDF_CloseDocument(d); }
    } doc{FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr)};
    if (!doc.d) throw InvalidInput("Invalid or encrypted PDF");

    const int count = FPDF_GetPageCount(doc.d);
    if (count <= 0) throw InvalidInput("PDF has no pages");
    pageCount = static_cast<uint32_t>(count);
    if (page >= pageCount) throw InvalidInput("PDF page out of range");

    struct Page {
        FPDF_PAGE p;
        ~Page() { if (p) FPDF_ClosePage(p); }
    } pg{FPDF_LoadPage(doc.d, static_cast<int>(page))};
    if (!pg.p) throw InvalidInput("Failed to load PDF page");

    const double pw = FPDF_GetPageWidth(pg.p);
    const double ph = FPDF_GetPageHeight(pg.p);
    if (!(pw > 0) || !(ph > 0) || !std::isfinite(pw) || !std::isfinite(ph)) throw InvalidInput("Invalid PDF page size");
    const double ratio = static_cast<double>(target) / std::max(pw, ph);
    const int w = std::max(1, static_cast<int>(std::lround(pw * ratio)));
    const int h = std::max(1, static_cast<int>(std::lround(ph * ratio)));
    if (pixels(w, h) > limits.maxPixels) throw LimitExceeded("PDF page exceeds the pixel limit");

    struct Bitmap {
        FPDF_BITMAP b;
        ~Bitmap() { if (b) FPDFBitmap_Destroy(b); }
    } bmp{FPDFBitmap_Create(w, h, 0)};
    if (!bmp.b) throw LimitExceeded("Could not allocate the PDF bitmap");
    FPDFBitmap_FillRect(bmp.b, 0, 0, w, h, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bmp.b, pg.p, 0, 0, w, h, 0, FPDF_ANNOT);

    const auto* raw = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bmp.b));
    const int stride = FPDFBitmap_GetStride(bmp.b);
    if (!raw || stride < w * 4) throw std::runtime_error("PDF bitmap unavailable");

    Raster r;
    r.width = static_cast<uint32_t>(w);
    r.height = static_cast<uint32_t>(h);
    r.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        const auto* row = raw + static_cast<std::ptrdiff_t>(y) * stride;
        auto* out = r.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3;
        for (int x = 0; x < w; ++x) {
            out[x * 3 + 0] = row[x * 4 + 2];
            out[x * 3 + 1] = row[x * 4 + 1];
            out[x * 3 + 2] = row[x * 4 + 0];
        }
    }
    return r;
}

Raster fit(const Raster& source, const uint32_t maxDim) {
    if (source.width == 0 || source.height == 0) throw InvalidInput("Empty raster");
    const auto longest = std::max(source.width, source.height);
    if (longest <= maxDim) return source;
    const double ratio = static_cast<double>(maxDim) / static_cast<double>(longest);
    Raster out;
    out.width = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(source.width * ratio)));
    out.height = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(source.height * ratio)));
    out.rgb.resize(static_cast<std::size_t>(out.width) * out.height * 3);
    if (!stbir_resize_uint8(source.rgb.data(), static_cast<int>(source.width), static_cast<int>(source.height), 0,
                            out.rgb.data(), static_cast<int>(out.width), static_cast<int>(out.height), 0, 3))
        throw std::runtime_error("Resize failed");
    return out;
}

std::vector<uint8_t> encodeJpeg(const Raster& raster, const int quality) {
    TjHandle tj(tjInitCompress());
    if (!tj) throw std::runtime_error("TurboJPEG unavailable");
    unsigned char* buffer = nullptr;
    unsigned long size = 0;
    if (tjCompress2(tj.get(), raster.rgb.data(), static_cast<int>(raster.width), 0, static_cast<int>(raster.height),
                    TJPF_RGB, &buffer, &size, TJSAMP_444, quality, 0) != 0) {
        if (buffer) tjFree(buffer);
        throw std::runtime_error(std::string("JPEG encode failed: ") + tjGetErrorStr2(tj.get()));
    }
    std::vector<uint8_t> out(buffer, buffer + size);
    tjFree(buffer);
    return out;
}

}
