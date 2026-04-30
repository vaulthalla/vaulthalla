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
#include "auth/session/Manager.hpp"
#include "share/Manager.hpp"
#include "share/TargetResolver.hpp"
#include "identities/User.hpp"

#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using namespace vh::config;
using namespace vh::stats::model;
using namespace vh::fs::model;
using namespace vh::fs::ops;

static std::unordered_map<std::string, std::string> parse_query_params(const std::string& target);
static bool isSharePreviewRequestTarget(const std::string& target);

namespace {
[[nodiscard]] std::shared_ptr<vh::protocols::ws::Session> defaultPreviewSessionResolver(
    const vh::protocols::http::request& req
) {
    if (isSharePreviewRequestTarget(std::string(req.target()))) {
        const auto shareRefresh = vh::protocols::extractCookie(req, "share_refresh");
        if (shareRefresh.empty()) throw std::runtime_error("Share refresh token not set");
        return vh::runtime::Deps::get().sessionManager->validateRawShareRefreshToken(shareRefresh);
    }

    const auto refresh = vh::protocols::extractCookie(req, "refresh");
    if (refresh.empty()) throw std::runtime_error("Refresh token not set");
    return vh::runtime::Deps::get().sessionManager->validateRawRefreshToken(refresh);
}

[[nodiscard]] std::shared_ptr<vh::share::Manager> defaultSharePreviewManager() {
    return std::make_shared<vh::share::Manager>();
}

[[nodiscard]] std::shared_ptr<vh::share::TargetResolver> defaultSharePreviewResolver() {
    return std::make_shared<vh::share::TargetResolver>();
}

[[nodiscard]] std::shared_ptr<vh::storage::Engine> defaultPreviewEngine(const uint32_t vaultId) {
    return vh::runtime::Deps::get().storageManager->getEngine(vaultId);
}

[[nodiscard]] vh::protocols::http::Router::PreviewSessionResolver& previewSessionResolver() {
    static vh::protocols::http::Router::PreviewSessionResolver resolver = defaultPreviewSessionResolver;
    return resolver;
}

[[nodiscard]] vh::protocols::http::Router::SharePreviewManagerFactory& sharePreviewManagerFactory() {
    static vh::protocols::http::Router::SharePreviewManagerFactory factory = defaultSharePreviewManager;
    return factory;
}

[[nodiscard]] vh::protocols::http::Router::SharePreviewResolverFactory& sharePreviewResolverFactory() {
    static vh::protocols::http::Router::SharePreviewResolverFactory factory = defaultSharePreviewResolver;
    return factory;
}

[[nodiscard]] vh::protocols::http::Router::PreviewEngineResolver& previewEngineResolver() {
    static vh::protocols::http::Router::PreviewEngineResolver resolver = defaultPreviewEngine;
    return resolver;
}
}

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

static bool isSharePreviewRequestTarget(const std::string& target) {
    const auto params = parse_query_params(target);
    return params.contains("share") && params.at("share") == "1";
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

static std::unique_ptr<vh::protocols::http::model::preview::Request> preparePreviewRequest(
    const std::unordered_map<std::string, std::string>& params
) {
    auto pr = std::make_unique<vh::protocols::http::model::preview::Request>(params);
    pr->engine = previewEngineResolver()(pr->vault_id);
    if (!pr->engine)
        throw std::runtime_error("No storage engine found for vault with id " + std::to_string(pr->vault_id));

    const auto fusePath = pr->engine->paths->absRelToAbsRel(pr->rel_path, PathType::VAULT_ROOT, PathType::FUSE_ROOT);
    const auto entry = vh::runtime::Deps::get().fsCache->getEntry(fusePath);
    if (!entry) throw std::runtime_error("File not found in cache");
    if (entry->isDirectory()) throw std::runtime_error("Requested preview file is a directory");
    pr->file = std::static_pointer_cast<File>(entry);
    return pr;
}

static std::unique_ptr<vh::protocols::http::model::preview::Request> prepareSharePreviewRequest(
    const std::unordered_map<std::string, std::string>& params,
    const std::shared_ptr<vh::protocols::ws::Session>& session
) {
    if (!params.contains("share") || params.at("share") != "1")
        throw std::runtime_error("Share preview requests must use the share preview lane");
    if (!session || !session->isShareMode() || session->user)
        throw std::runtime_error("Share preview requires a ready share session");
    if (session->shareSessionToken().empty())
        throw std::runtime_error("Share session token is missing");

    auto manager = sharePreviewManagerFactory()();
    if (!manager) throw std::runtime_error("Share preview manager is unavailable");
    auto resolver = sharePreviewResolverFactory()();
    if (!resolver) throw std::runtime_error("Share preview resolver is unavailable");

    auto principal = manager->resolvePrincipal(
        session->shareSessionToken(),
        session->ipAddress.empty() ? std::nullopt : std::make_optional(session->ipAddress),
        session->userAgent.empty() ? std::nullopt : std::make_optional(session->userAgent)
    );
    session->setSharePrincipal(principal, session->shareSessionToken());

    const auto actor = session->rbacActor();
    const auto target = resolver->resolve(actor, {
        .path = params.contains("path") ? params.at("path") : std::string{"/"},
        .operation = vh::share::Operation::Preview,
        .path_mode = vh::share::TargetPathMode::ShareRelative,
        .expected_target_type = vh::share::TargetType::File
    });

    std::unordered_map<std::string, std::string> scopedParams{
        {"vault_id", std::to_string(target.vault_id)},
        {"path", target.vault_path}
    };
    if (params.contains("size")) scopedParams["size"] = params.at("size");
    if (params.contains("scale")) scopedParams["scale"] = params.at("scale");

    auto pr = std::make_unique<vh::protocols::http::model::preview::Request>(scopedParams);
    pr->engine = previewEngineResolver()(pr->vault_id);
    if (!pr->engine)
        throw std::runtime_error("No storage engine found for scoped share preview");

    const auto file = std::dynamic_pointer_cast<File>(target.entry);
    if (!file) throw std::runtime_error("Share preview target is not a file");
    pr->file = file;

    manager->appendAccessAuditEvent(*principal, {
        .event_type = "share.preview.http",
        .target = {
            .vault_id = target.vault_id,
            .target_entry_id = target.entry ? target.entry->id : 0,
            .target_path = target.vault_path
        },
        .status = vh::share::AuditStatus::Success,
        .bytes_transferred = std::nullopt,
        .error_code = std::nullopt,
        .error_message = std::nullopt
    });

    return pr;
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

void Router::setPreviewSessionResolverForTesting(PreviewSessionResolver resolver) {
    if (!resolver) throw std::invalid_argument("Preview session resolver is required");
    previewSessionResolver() = std::move(resolver);
}

void Router::resetPreviewSessionResolverForTesting() {
    previewSessionResolver() = defaultPreviewSessionResolver;
}

void Router::setSharePreviewManagerFactoryForTesting(SharePreviewManagerFactory factory) {
    if (!factory) throw std::invalid_argument("Share preview manager factory is required");
    sharePreviewManagerFactory() = std::move(factory);
}

void Router::resetSharePreviewManagerFactoryForTesting() {
    sharePreviewManagerFactory() = defaultSharePreviewManager;
}

void Router::setSharePreviewResolverFactoryForTesting(SharePreviewResolverFactory factory) {
    if (!factory) throw std::invalid_argument("Share preview resolver factory is required");
    sharePreviewResolverFactory() = std::move(factory);
}

void Router::resetSharePreviewResolverFactoryForTesting() {
    sharePreviewResolverFactory() = defaultSharePreviewResolver;
}

void Router::setPreviewEngineResolverForTesting(PreviewEngineResolver resolver) {
    if (!resolver) throw std::invalid_argument("Preview engine resolver is required");
    previewEngineResolver() = std::move(resolver);
}

void Router::resetPreviewEngineResolverForTesting() {
    previewEngineResolver() = defaultPreviewEngine;
}

Response Router::handlePreview(request&& req) {
    if (req.method() != verb::get || !req.target().starts_with("/preview"))
        return makeErrorResponse(
            req, "Invalid request", status::bad_request);

    const auto params = parse_query_params(std::string(req.target()));
    const auto sharePreview = isSharePreviewRequestTarget(std::string(req.target()));

    std::shared_ptr<vh::protocols::ws::Session> session;
    try {
        session = previewSessionResolver()(req);
    } catch (const std::exception& e) {
        return makeErrorResponse(req, "Unauthorized: " + std::string(e.what()), status::unauthorized);
    }

    std::unique_ptr<Request> pr;
    try {
        if (sharePreview) {
            if (!session || !session->isShareMode() || session->user)
                return makeErrorResponse(req, "Unauthorized: preview requires a ready share session", status::unauthorized);
            pr = prepareSharePreviewRequest(params, session);
        }
        else {
            if (!session || !session->user)
                return makeErrorResponse(req, "Unauthorized: preview requires a user", status::unauthorized);
            pr = preparePreviewRequest(params);
        }
    } catch (const std::exception& e) {
        return makeErrorResponse(req, e.what(), status::bad_request);
    }

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
