#include "protocols/http/handler/Handlers.hpp"

#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "preview/Plan.hpp"
#include "preview/cache/Store.hpp"
#include "preview/derive/Queue.hpp"
#include "protocols/http/Access.hpp"
#include "protocols/http/handler/Common.hpp"
#include "storage/PlaintextReader.hpp"

#include <nlohmann/json.hpp>

namespace vh::protocols::http::handler {

namespace {

[[nodiscard]] std::string contentTypeFor(const std::string& kind) {
    if (kind == "model-glb") return "model/gltf-binary";
    if (kind == "poster-jpg") return "image/jpeg";
    if (kind == "probe-json") return "application/json";
    if (kind.starts_with("transcode-")) return "video/mp4";
    return "application/octet-stream";
}

[[nodiscard]] model::preview::Response statusJson(const request& req, const status code, nlohmann::json body,
                                                  const std::optional<uint32_t> retryAfter = std::nullopt) {
    string_response res{code, req.version()};
    res.set(field::content_type, "application/json");
    res.set(field::cache_control, "no-store");
    if (retryAfter) res.set(field::retry_after, std::to_string(*retryAfter));
    res.body() = body.dump();
    res.prepare_payload();
    res.keep_alive(req.keep_alive());
    return res;
}

}

model::preview::Response derived(request&& req) {
    try {
        const auto params = access::parseQuery(std::string(req.target()));
        const auto kindIt = params.find("kind");
        if (kindIt == params.end() || kindIt->second.empty())
            return jsonError(req, status::bad_request, "invalid", "Missing required parameter: kind");
        const auto& kind = kindIt->second;
        const auto capability = preview::derivedKindCapability(kind);
        if (!capability || !preview::derive::kindKnown(kind))
            return jsonError(req, status::bad_request, "invalid", "Unknown derived artifact kind");
        const auto variantIt = params.find("variant");
        const std::string variant = variantIt == params.end() || variantIt->second.empty() ? "v1" : variantIt->second;

        const auto caller = access::authenticate(req, params);
        const auto need = *capability == preview::Capability::Download ? access::Need::Download : access::Need::Preview;
        const auto target = access::resolve(caller, params, need, access::Expect::File);

        const auto result = preview::derive::Queue::instance().request(target.engine, target.file, kind, variant);
        using preview::derive::DeriveStatus;
        switch (result.status) {
            case DeriveStatus::Queued:
                return statusJson(req, status::accepted, {{"status", "queued"}}, result.retryAfterSeconds);
            case DeriveStatus::Busy:
                return statusJson(req, status::service_unavailable, {{"status", "busy"}, {"code", "busy"}},
                                  result.retryAfterSeconds);
            case DeriveStatus::Failed:
                return statusJson(req, status::unprocessable_entity,
                                  {{"status", "failed"}, {"code", "conversion_failed"}, {"reason", result.reason}});
            case DeriveStatus::Unavailable:
                return statusJson(req, status::service_unavailable,
                                  {{"status", "unavailable"}, {"code", "converter_unavailable"}, {"helper", result.helper}});
            case DeriveStatus::Unsupported:
                return statusJson(req, status::unsupported_media_type,
                                  {{"status", "unsupported"}, {"code", "unsupported"}, {"reason", result.reason}});
            case DeriveStatus::Ready:
                break;
        }
        if (!result.artifact) throw std::runtime_error("Derived artifact missing after a ready result");

        const auto artifact = *result.artifact;
        const auto engine = target.engine;
        auto etag = storage::generationOf(*target.file).etag();
        etag.insert(etag.size() - 1, "-" + kind + "-" + variant + "-g" + std::to_string(artifact.key.generator_version));

        ServeSpec spec;
        spec.size = artifact.size;
        spec.etag = etag;
        spec.lastModified = target.file->updated_at;
        spec.contentType = contentTypeFor(kind);
        spec.cacheControl = "private, max-age=0, must-revalidate, no-transform";
        spec.capOpenEndedRanges = kind.starts_with("transcode-");
        addOriginalBytesHardening(spec);
        spec.open = [engine, artifact]() -> std::shared_ptr<storage::PlaintextReader> {
            return preview::cache::Store::open(engine, artifact);
        };
        spec.admit = [target, caller, kind, capability](bool) {
            const bool download = *capability == preview::Capability::Download;
            if (target.share) return access::recordShareAccess(target, "share.derived.http", download, std::nullopt);
            access::recordHumanAccess(caller, target, "derived." + kind);
            return true;
        };
        return serve(req, std::move(spec));
    } catch (const std::exception& e) {
        return errorFor(req, e);
    }
}

}
