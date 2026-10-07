// Render-path bounds: decompression bombs are refused from the header, before any large allocation.
#include "preview/render/Raster.hpp"

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <string>
#include <vector>

namespace vh::preview::render::test_render {

namespace {
std::vector<uint8_t> jpeg(const int w, const int h, const bool progressive) {
    std::vector<uint8_t> rgb(static_cast<std::size_t>(w) * h * 3, 90);
    tjhandle tj = tjInitCompress();
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    tjCompress2(tj, rgb.data(), w, 0, h, TJPF_RGB, &buf, &size, TJSAMP_420, 80, progressive ? TJFLAG_PROGRESSIVE : 0);
    std::vector<uint8_t> out(buf, buf + size);
    tjFree(buf);
    tjDestroy(tj);
    return out;
}
}

TEST(PreviewRender, ProgressiveJpegsGetHalfThePixelBudgetBecauseTheirDecoderBuffersFullResolution) {
    const Limits limits{.maxSourceBytes = 64ull << 20, .maxPixels = 10'000'000, .maxDimension = 2048};
    const auto baseline = jpeg(4000, 2000, false);     // 8 MP
    const auto progressive = jpeg(4000, 2000, true);   // 8 MP > 10/2
    const auto small = decodeImage(baseline, 256, limits);
    EXPECT_LE(std::max(small.width, small.height), 1000u);  // DCT-scaled decode, not 4000 px wide
    EXPECT_THROW((void)decodeImage(progressive, 256, limits), LimitExceeded);
    EXPECT_NO_THROW((void)decodeImage(jpeg(2000, 2000, true), 256, limits));  // 4 MP progressive is fine
}

TEST(PreviewRender, HugeDeclaredDimensionsAreRefusedFromTheHeader) {
    const Limits limits{.maxSourceBytes = 64ull << 20, .maxPixels = 64'000'000, .maxDimension = 2048};
    auto bomb = jpeg(16, 16, false);
    // Patch the SOF0 frame header to claim 60000 x 60000.
    for (std::size_t i = 0; i + 9 < bomb.size(); ++i)
        if (bomb[i] == 0xFF && bomb[i + 1] == 0xC0) {
            bomb[i + 5] = 0xEA; bomb[i + 6] = 0x60;  // height 60000
            bomb[i + 7] = 0xEA; bomb[i + 8] = 0x60;  // width 60000
            break;
        }
    EXPECT_THROW((void)decodeImage(bomb, 256, limits), LimitExceeded);
}

TEST(PreviewRender, FormatsOutsideTheRenderSetAreNotParsedInTheDaemon) {
    const Limits limits{};
    const std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 16 +X 16\n";
    EXPECT_THROW((void)decodeImage({reinterpret_cast<const uint8_t*>(hdr.data()), hdr.size()}, 64, limits), InvalidInput);
    const std::string psd = "8BPS\x00\x01";
    EXPECT_THROW((void)decodeImage({reinterpret_cast<const uint8_t*>(psd.data()), psd.size()}, 64, limits), InvalidInput);
}

TEST(PreviewRender, FitNeverUpscales) {
    Raster r{.rgb = std::vector<uint8_t>(10 * 5 * 3, 1), .width = 10, .height = 5};
    const auto out = fit(r, 1024);
    EXPECT_EQ(out.width, 10u);
    EXPECT_EQ(out.height, 5u);
}

}
