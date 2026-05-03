#include "protocols/http/handler/preview/Image.hpp"
#include "preview/image.hpp"
#include "fs/ops/file.hpp"
#include "log/Registry.hpp"
#include "protocols/http/model/preview/Request.hpp"
#include "protocols/http/Router.hpp"

#include <boost/beast/http/file_body.hpp>

using namespace vh::storage;
using namespace vh::fs::ops;
using namespace vh::preview;

namespace vh::protocols::http::handler::preview {
    model::preview::Response handler::preview::Image::handle(request &&req,
                                                             const std::unique_ptr<model::preview::Request> &&pr) {
        try {
            const auto tmpPath = decrypt_file_to_temp(pr->file, pr->engine);

            if (pr->size || pr->scale) {
                std::vector<uint8_t> resized = image::resize_and_compress(tmpPath, pr->scaleStr(), pr->sizeStr());
                return Router::makeResponse(req, std::move(resized), "image/jpeg");
            }

            boost::beast::error_code ec;
            http::file_body::value_type body;
            body.open(tmpPath.c_str(), boost::beast::file_mode::scan, ec);
            if (ec) return Router::makeErrorResponse(req, "File not found.", http::status::not_found);

            return Router::makeResponse(req, std::move(body), "image/jpeg");
        } catch (const std::exception &e) {
            log::Registry::http()->error("[ImagePreviewHandler] Error handling image preview for {}: {}",
                                         pr->rel_path.string(), e.what());
            return Router::makeErrorResponse(req, "Failed to load image: " + std::string(e.what()),
                                             http::status::unsupported_media_type);
        }
    }
}
