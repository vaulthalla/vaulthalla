#include "preview/pdf.hpp"
#include "preview/image.hpp"

#include <stdexcept>
#include <pdfium/fpdfview.h>

namespace vh::preview::pdf {

std::vector<uint8_t> resize_and_compress_buffer(
    const uint8_t* data, size_t size,
    const std::optional<std::string>& scale_opt,
    const std::optional<std::string>& size_opt) {

    FPDF_DOCUMENT doc = FPDF_LoadMemDocument(data, static_cast<int>(size), nullptr);
    if (!doc) {
        throw std::runtime_error("Failed to load PDF from memory");
    }

    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    if (!page) {
        FPDF_CloseDocument(doc);
        throw std::runtime_error("Failed to load first page");
    }

    const int width = static_cast<int>(FPDF_GetPageWidth(page));
    const int height = static_cast<int>(FPDF_GetPageHeight(page));

    int new_w = width;
    int new_h = height;
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

    FPDF_BITMAP bitmap = FPDFBitmap_Create(new_w, new_h, 0); // 0 = BGRx
    FPDFBitmap_FillRect(bitmap, 0, 0, new_w, new_h, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, new_w, new_h, 0, 0);

    const auto* raw = static_cast<uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
    std::vector<uint8_t> rgb(new_w * new_h * 3);

    for (int y = 0; y < new_h; ++y) {
        for (int x = 0; x < new_w; ++x) {
            const int idx = y * new_w + x;
            rgb[idx * 3 + 0] = raw[idx * 4 + 2]; // R
            rgb[idx * 3 + 1] = raw[idx * 4 + 1]; // G
            rgb[idx * 3 + 2] = raw[idx * 4 + 0]; // B
        }
    }

    std::vector<uint8_t> jpeg_buf;
    image::compress_to_jpeg(rgb.data(), new_w, new_h, jpeg_buf);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);

    return jpeg_buf;
}

}
