// Render-path bounds (decompression bombs are refused from the header, before any large allocation) and PDF
// rendering straight into packed RGB.
#include "preview/render/Raster.hpp"

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <array>
#include <cstdio>
#include <optional>
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

// One 600x100 pt page: red, green and blue path fills, then a 2x1 inline RGB image (orange, steel blue) scaled over
// the right half, so both the path and the image compositors are checked for channel order.
std::vector<uint8_t> colorBandsPdf() {
    const std::string content =
        "1 0 0 rg 0 0 100 100 re f 0 1 0 rg 100 0 100 100 re f 0 0 1 rg 200 0 100 100 re f "
        "q 300 0 0 100 300 0 cm BI /W 2 /H 1 /CS /RGB /BPC 8 ID " +
        std::string("\xff\x80\x00\x00\x40\xc0", 6) + " EI Q";  // explicit length: the pixels contain NULs
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 100] /Contents 4 0 R >>",
        "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream",
    };
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto off : offsets) {
        char line[21];
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", off);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return {pdf.begin(), pdf.end()};
}

std::array<uint8_t, 3> pixelAt(const Raster& r, const double fx) {
    const auto x = static_cast<std::size_t>(fx * r.width);
    const auto i = (static_cast<std::size_t>(r.height / 2) * r.width + x) * 3;
    return {r.rgb[i], r.rgb[i + 1], r.rgb[i + 2]};
}
}

class PreviewRenderPdf : public ::testing::Test {
protected:
    inline static std::optional<PdfiumLibrary> pdfium;
    static void SetUpTestSuite() { pdfium.emplace(); }
    static void TearDownTestSuite() { pdfium.reset(); }
};

// PDFium renders into the packed RGB raster itself (24bpp + FPDF_REVERSE_BYTE_ORDER). An odd width makes the row
// stride (301 * 3 bytes) unaligned, and a channel-order regression would still produce a valid, wrongly tinted JPEG.
TEST_F(PreviewRenderPdf, PagesRenderStraightIntoRgbInTrueChannelOrder) {
    const auto pdf = colorBandsPdf();
    uint32_t pages = 0;
    const auto r = renderPdfPage(pdf, 0, 301, Limits{}, pages);
    EXPECT_EQ(pages, 1u);
    ASSERT_EQ(r.width, 301u);
    ASSERT_EQ(r.height, 50u);
    ASSERT_EQ(r.rgb.size(), 301u * 50u * 3u);
    EXPECT_EQ(pixelAt(r, 1.0 / 12), (std::array<uint8_t, 3>{255, 0, 0}));
    EXPECT_EQ(pixelAt(r, 3.0 / 12), (std::array<uint8_t, 3>{0, 255, 0}));
    EXPECT_EQ(pixelAt(r, 5.0 / 12), (std::array<uint8_t, 3>{0, 0, 255}));
    EXPECT_EQ(pixelAt(r, 7.0 / 12), (std::array<uint8_t, 3>{255, 128, 0}));
    EXPECT_EQ(pixelAt(r, 11.0 / 12), (std::array<uint8_t, 3>{0, 64, 192}));
    EXPECT_THROW((void)renderPdfPage(pdf, 1, 301, Limits{}, pages), InvalidInput);
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
