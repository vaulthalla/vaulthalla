#include "protocols/http/Router.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "config/Registry.hpp"
#include "auth/Manager.hpp"
#include "protocols/http/handler/preview/Image.hpp"
#include "protocols/http/handler/preview/Pdf.hpp"
#include "fs/ops/file.hpp"
#include "runtime/Deps.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "fs/cache/Registry.hpp"
#include "stats/model/CacheStats.hpp"
#include "protocols/http/model/preview/Request.hpp"
#include "log/Registry.hpp"
#include "protocols/cookie.hpp"
#include "protocols/ws/Session.hpp"
#include "../../../include/identities/User.hpp"

#include <nlohmann/json.hpp>

using namespace vh::config;
using namespace vh::stats::model;
using namespace vh::fs::model;
using namespace vh::fs::ops;

static std::string url_decode(const std::string& value) {
    std::ostringstream result;
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%' && i + 2 < value.length()) {
            if (int hex = 0; std::istringstream(value.substr(i + 1, 2)) >> std::hex >> hex) {
                result << static_cast<char>(hex);
                i += 2;
            } else throw std::runtime_error("Invalid percent-encoding in URL");
        }
        else if (value[i] == '+') result << ' ';
        else result << value[i];
    }
    return result.str();
}

static std::unordered_map<std::string, std::string> parse_query_params(const std::string& target) {
    std::unordered_map<std::string, std::string> params;

    const auto pos = target.find('?');
    if (pos == std::string::npos) return params;

    const std::string query = target.substr(pos + 1);
    std::istringstream stream(query);
    std::string pair;

    while (std::getline(stream, pair, '&')) {
        const auto eq = pair.find('=');
        if (eq != std::string::npos) {
            const auto key = url_decode(pair.substr(0, eq));
            const auto value = url_decode(pair.substr(eq + 1));
            params[key] = value;
        }
    }

    return params;
}

static std::vector<uint8_t> tryCacheRead(const std::shared_ptr<File>& f, const std::filesystem::path& thumbnailRoot,
                                         const unsigned int size) {
    if (const auto thumbnail_sizes = Registry::get().caching.thumbnails.sizes;
        std::ranges::find(thumbnail_sizes.begin(), thumbnail_sizes.end(), size) != thumbnail_sizes.end()) {

        if (const auto pathToJpegCache = thumbnailRoot / f->base32_alias / std::string(std::to_string(size) + ".jpg");
            f->mime_type && (f->mime_type->starts_with("image/") || f->mime_type->starts_with("application/"))
            && std::filesystem::exists(pathToJpegCache))
            return readFileToVector(pathToJpegCache);

        vh::runtime::Deps::get().httpCacheStats->record_miss();
        }
    return {};
}
namespace vh::protocols::http {

using namespace vh::protocols::http::model::preview;

Response Router::route(request&& req) {
    if (req.method() != verb::get)
        return makeErrorResponse(req, "Invalid request", status::bad_request);

    if (req.target().starts_with("/auth/session"))
        return handleAuthSession(std::move(req));

    if (req.target().starts_with("/preview"))
        return handlePreview(std::move(req));

    return makeErrorResponse(req, "Not found", status::not_found);
}

Response Router::handleAuthSession(request&& req) {
    if (Registry::get().dev.enabled) return makeJsonResponse(req, nlohmann::json{{"ok", true}});

    try {
        const auto refresh = protocols::extractCookie(req, "refresh");
        if (refresh.empty()) {
            log::Registry::http()->warn("[Router] Refresh token not set");
            return makeErrorResponse(req, "Refresh token not set", status::bad_request);
        }
        const auto session = runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh);

        // Could mint an access token here if we wanted to
        nlohmann::json j{
                    {"ok", true},
                    {"user_id", session->user->id},
                    // {"access_token", access.token},
                    // {"expires_in", access.expires_in}
                };

        return makeJsonResponse(req, j);
    } catch (const std::exception& e) {
        log::Registry::http()->warn("[Router]: Invalid refresh token: %s", e.what());
        return makeErrorResponse(req, std::string("Unauthorized: ") + e.what(), status::unauthorized);
    }
}

std::string Router::authenticateRequest(const request& req) {
    if (Registry::get().dev.enabled) return "";
    try {
        const auto refresh_token = protocols::extractCookie(req, "refresh");
        runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh_token);
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

Response Router::handlePreview(request&& req) {
    if (req.method() != verb::get || !req.target().starts_with("/preview"))
        return makeErrorResponse(
            req, "Invalid request", status::bad_request);

    if (const auto error = authenticateRequest(req); !error.empty())
        return makeErrorResponse(
            req, "Unauthorized: " + error, status::unauthorized);

    std::unique_ptr<Request> pr;
    try { pr = std::make_unique<Request>(parse_query_params(req.target())); } catch (const std::exception&
        e) { return makeErrorResponse(req, e.what(), status::bad_request); }

    pr->engine = runtime::Deps::get().storageManager->getEngine(pr->vault_id);
    if (!pr->engine)
        return makeErrorResponse(
            req, "No storage engine found for vault with id " + std::to_string(pr->vault_id),
            status::bad_request);

    const auto fusePath = pr->engine->paths->absRelToAbsRel(pr->rel_path, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    const auto entry = runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) return makeErrorResponse(req, "File not found in cache");
    if (entry->isDirectory())
        return makeErrorResponse(req, "Requested preview file is a directory", status::bad_request);
    pr->file = std::static_pointer_cast<File>(entry);

    if (!pr->file->mime_type)
        return makeErrorResponse(req, "File has no mime type.", status::unsupported_media_type);

    if (pr->size)
        if (auto data = tryCacheRead(pr->file, pr->engine->paths->thumbnailRoot, *pr->size); !data.empty())
            return makeResponse(req, std::move(data), "image/jpeg", true);

    if (pr->file->mime_type->starts_with("image/") || pr->file->mime_type->ends_with("/pdf")) {
        ScopedOpTimer timer(runtime::Deps::get().httpCacheStats.get());
        return pr->file->mime_type->starts_with("image/")
                   ? handler::preview::Image::handle(std::move(req), std::move(pr))
                   : handler::preview::Pdf::handle(std::move(req), std::move(pr));
    }

    return makeErrorResponse(
        req, "Unsupported preview type: " + (pr->file->mime_type ? *pr->file->mime_type : "unknown"));
}

Response Router::makeResponse(const request& req, std::vector<uint8_t>&& data,
                                         const std::string& mime_type, const bool cacheHit) {
    const auto size = data.size();

    vector_response res{
        std::piecewise_construct,
        std::make_tuple(std::move(data)),
        std::make_tuple(status::ok, req.version())
    };

    res.set(field::content_type, mime_type);
    res.content_length(size);
    res.keep_alive(req.keep_alive());

    if (cacheHit) runtime::Deps::get().httpCacheStats->record_hit(size);

    return res;
}

Response Router::makeResponse(const request& req, file_body::value_type data,
                                         const std::string& mime_type, const bool cacheHit) {
    const auto size = data.size();

    file_response res{
        std::piecewise_construct,
        std::make_tuple(std::move(data)),
        std::make_tuple(status::ok, req.version())
    };

    res.set(field::content_type, mime_type);
    res.content_length(size);
    res.keep_alive(req.keep_alive());

    if (cacheHit) runtime::Deps::get().httpCacheStats->record_hit(size);

    return res;
}

Response Router::makeJsonResponse(const request& req, const nlohmann::json& j) {
    string_response res{status::ok, req.version()};
    res.set(field::content_type, "application/json");
    res.body() = j.dump();
    res.prepare_payload();
    res.keep_alive(req.keep_alive());
    return res;
}

Response Router::makeErrorResponse(const request& req,
                                              const std::string& msg,
                                              const status& status) {
    string_response res{status, req.version()};
    res.set(field::content_type, "text/plain");
    res.body() = msg;
    res.prepare_payload();
    return res;
}

}
