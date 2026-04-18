#include "preview/thumbnail/ops.hpp"
#include "preview/image.hpp"
#include "preview/pdf.hpp"

#include <stdexcept>
#include <fstream>
#include <iomanip>

namespace vh::preview::thumbnail {

void generateAndStore(const std::vector<uint8_t>& buffer, const std::filesystem::path& outputPath,
                               const std::string& mime, const unsigned int size) {
    std::vector<uint8_t> jpeg;

    if (mime.starts_with("image/")) {
        jpeg = image::resize_and_compress_buffer(
            buffer.data(), buffer.size(), std::nullopt, std::make_optional(std::to_string(size))
            );
    } else if (mime == "application/pdf") {
        jpeg = pdf::resize_and_compress_buffer(
            buffer.data(), buffer.size(), std::nullopt, std::make_optional(std::to_string(size))
            );
    } else throw std::runtime_error("Unsupported MIME type for thumbnail generation: " + mime);

    if (jpeg.empty()) throw std::runtime_error("Thumbnail JPEG buffer is empty after processing");

    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) throw std::runtime_error("Failed to open thumbnail output path: " + outputPath.string());

    out.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
    if (!out.good()) throw std::runtime_error("Failed to write thumbnail to disk: " + outputPath.string());

    out.close();
}

}
