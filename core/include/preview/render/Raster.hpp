#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

// Lossy server-side rendering (images and PDF pages -> JPEG), bounded against hostile input: every decode is
// preceded by a header check against the pixel cap, JPEGs decode at the smallest DCT scale that still covers the
// target size, PDF rendering is serialized behind one process-wide lock (PDFium is not thread-safe), and nothing
// touches the disk.
namespace vh::preview::render {

struct Limits {
    uint64_t maxSourceBytes{512ull * 1024 * 1024};
    uint64_t maxPixels{100'000'000};   // decoded RGB pixels
    uint32_t maxDimension{2048};       // output edge cap
};

[[nodiscard]] Limits limitsFromConfig();

struct Raster {
    std::vector<uint8_t> rgb;  // packed RGB8
    uint32_t width{};
    uint32_t height{};
};

class InvalidInput final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class LimitExceeded final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decodes an image whose longest edge will be fit into targetMaxDim. JPEG: TurboJPEG with DCT scaling (only the
// pixels needed); other formats: stb_image from memory. Throws InvalidInput / LimitExceeded.
[[nodiscard]] Raster decodeImage(std::span<const uint8_t> bytes, uint32_t targetMaxDim, const Limits& limits);

// Renders one PDF page so that its longest edge is targetMaxDim (never above limits.maxDimension). pageCount is
// set to the document's page count. Throws InvalidInput (bad document / page out of range) / LimitExceeded.
[[nodiscard]] Raster renderPdfPage(std::span<const uint8_t> bytes, uint32_t page, uint32_t targetMaxDim,
                                   const Limits& limits, uint32_t& pageCount);

// Downscales to fit maxDim (never upscales; returns a copy when already small enough).
[[nodiscard]] Raster fit(const Raster& source, uint32_t maxDim);

// Baseline JPEG, 4:4:4, quality 85 (the thumbnail quality Vaulthalla has always shipped).
[[nodiscard]] std::vector<uint8_t> encodeJpeg(const Raster& raster, int quality = 85);

// PDFium's process-wide init/teardown. Hold exactly one for as long as anything may render a PDF (the daemon's main,
// test suites); every render serializes behind the same lock as init and destroy.
class PdfiumLibrary {
public:
    PdfiumLibrary();
    ~PdfiumLibrary();
    PdfiumLibrary(const PdfiumLibrary&) = delete;
    PdfiumLibrary& operator=(const PdfiumLibrary&) = delete;
};

}
