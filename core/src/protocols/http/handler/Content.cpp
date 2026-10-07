#include "protocols/http/handler/Handlers.hpp"

#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "protocols/http/Access.hpp"
#include "protocols/http/handler/Archive.hpp"
#include "protocols/http/handler/Common.hpp"
#include "storage/Engine.hpp"
#include "storage/PlaintextReader.hpp"

namespace vh::protocols::http::handler {

namespace {

[[nodiscard]] bool wantsInline(const access::Params& params, const bool contentLane) {
    const auto it = params.find("disposition");
    if (it == params.end()) return contentLane;
    return it->second != "attachment";
}

[[nodiscard]] model::preview::Response directory(const request& req, const access::Caller& caller,
                                                 const access::Target& target) {
    const auto name = target.entry->name.empty() ? std::string{"download"} : target.entry->name;
    auto filename = name;
    if (!filename.ends_with(".zip")) filename += ".zip";

    if (req.method() == verb::head) {
        // Authorization already ran; building the archive just to measure it would cost the full read.
        model::preview::StreamResponse res;
        res.version(req.version());
        res.keep_alive(req.keep_alive());
        res.result(status::ok);
        res.set(field::content_type, "application/zip");
        res.set(field::content_disposition, Router::attachmentContentDisposition(filename));
        res.set(field::cache_control, "no-store");
        res.set("X-Accel-Buffering", "no");
        res.headOnly = true;
        res.omitContentLength = true;
        return res;
    }

    if (target.share && !access::recordShareAccess(target, "share.download.http", true, std::nullopt))
        return jsonError(req, status::forbidden, "max_downloads_reached", "This link's download limit was reached");
    if (!target.share) access::recordHumanAccess(caller, target, "download.archive");

    auto data = archive::build(caller, target);
    auto res = Router::makeDownloadResponse(req, std::move(data), "application/zip", filename);
    std::visit([](auto& r) { r.set("X-Accel-Buffering", "no"); }, res);
    return res;
}

}

model::preview::Response content(request&& req) {
    try {
        const auto target = std::string(req.target());
        const bool contentLane = target.starts_with("/download/content");
        const auto params = access::parseQuery(target);
        const auto caller = access::authenticate(req, params);
        const auto resolved = access::resolve(caller, params, access::Need::Download,
                                              contentLane ? access::Expect::File : access::Expect::Any);

        if (!resolved.file) {
            if (contentLane) return jsonError(req, status::bad_request, "invalid", "Target is a directory");
            return directory(req, caller, resolved);
        }

        const auto& file = resolved.file;
        const bool inlineDisposition = wantsInline(params, contentLane);
        const auto generation = storage::generationOf(*file);
        const auto mime = file->mime_type.value_or("application/octet-stream");

        ServeSpec spec;
        spec.size = file->size_bytes;
        spec.etag = generation.etag();
        spec.lastModified = file->updated_at;
        spec.contentType = safeContentType(mime, inlineDisposition);
        spec.contentDisposition = contentDisposition(inlineDisposition, file->name.empty() ? "download" : file->name);
        spec.cacheControl = inlineDisposition ? "private, max-age=0, must-revalidate, no-transform" : "no-store";
        spec.capOpenEndedRanges = inlineDisposition;
        addOriginalBytesHardening(spec);

        const auto engine = resolved.engine;
        spec.open = [engine, file]() -> std::shared_ptr<storage::PlaintextReader> {
            return engine->openPlaintextReader(file);
        };
        spec.admit = [resolved, caller, inlineDisposition](bool) {
            if (resolved.share)
                return access::recordShareAccess(resolved, inlineDisposition ? "share.content.http" : "share.download.http",
                                                 true, resolved.file->size_bytes);
            access::recordHumanAccess(caller, resolved, inlineDisposition ? "content" : "download");
            return true;
        };
        return serve(req, std::move(spec));
    } catch (const std::exception& e) {
        return errorFor(req, e);
    }
}

}
