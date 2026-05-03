#include "protocols/http/handler/preview/Pdf.hpp"
#include "preview/image.hpp"
#include "fs/ops/file.hpp"
#include "fs/model/File.hpp"
#include "log/Registry.hpp"
#include "protocols/http/model/preview/Request.hpp"
#include "protocols/http/Router.hpp"

#include <format>
#include <pdfium/fpdfview.h>

using namespace vh::fs::model;
using namespace vh::fs::ops;
using namespace vh::preview;

namespace vh::protocols::http::handler::preview {
    model::preview::Response Pdf::handle(request &&req, const std::unique_ptr<model::preview::Request> &&pr) {
        try {
            std::string mime_type = "image/jpeg";

            const auto tmpPath = decrypt_file_to_temp(pr->file, pr->engine);

            FPDF_DOCUMENT doc = FPDF_LoadDocument(tmpPath.c_str(), nullptr);
            if (!doc) throw std::runtime_error("Failed to load PDF");

            FPDF_PAGE page = FPDF_LoadPage(doc, 0);
            if (!page) {
                FPDF_CloseDocument(doc);
                throw std::runtime_error("Failed to load first page");
            }

            const int width = static_cast<int>(FPDF_GetPageWidth(page));
            const int height = static_cast<int>(FPDF_GetPageHeight(page));

            int new_w = width, new_h = height;
            if (pr->scale) {
                new_w = static_cast<int>(static_cast<float>(width) * *pr->scale);
                new_h = static_cast<int>(static_cast<float>(height) * *pr->scale);
            } else if (pr->size) {
                const float ratio = std::min(static_cast<float>(*pr->size) / static_cast<float>(width),
                                             static_cast<float>(*pr->size) / static_cast<float>(height));
                new_w = static_cast<int>(static_cast<float>(width) * ratio);
                new_h = static_cast<int>(static_cast<float>(height) * ratio);
            }

            FPDF_BITMAP bitmap = FPDFBitmap_Create(new_w, new_h, 0);
            FPDFBitmap_FillRect(bitmap, 0, 0, new_w, new_h, 0xFFFFFFFF);
            FPDF_RenderPageBitmap(bitmap, page, 0, 0, new_w, new_h, 0, 0);

            const auto *buffer = static_cast<uint8_t *>(FPDFBitmap_GetBuffer(bitmap));
            std::vector<uint8_t> rgb_data(new_w * new_h * 3);
            const int stride = FPDFBitmap_GetStride(bitmap);


            for (int y = 0; y < new_h; ++y) {
                const uint8_t *row = buffer + y * stride;
                for (int x = 0; x < new_w; ++x) {
                    const uint8_t *px = row + x * 4;
                    const int idx = (y * new_w + x) * 3;
                    rgb_data[idx + 0] = px[2]; // R
                    rgb_data[idx + 1] = px[1]; // G
                    rgb_data[idx + 2] = px[0]; // B
                }
            }

            std::vector<uint8_t> jpeg_buf;
            image::compress_to_jpeg(rgb_data.data(), new_w, new_h, jpeg_buf);

            FPDFBitmap_Destroy(bitmap);
            FPDF_ClosePage(page);
            FPDF_CloseDocument(doc);

            return Router::makeResponse(req, std::move(jpeg_buf), mime_type);
        } catch (const std::exception &e) {
            log::Registry::http()->error("[PdfPreviewHandler] Error handling PDF preview for {}: {}",
                                         pr->rel_path.string(), e.what());
            return Router::makeErrorResponse(req, std::string(e.what()), http::status::unsupported_media_type);
        }
    }
}
