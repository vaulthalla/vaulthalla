#pragma once

#include "protocols/http/Router.hpp"

#include <ctime>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vh::storage {
    class PlaintextReader;
}

namespace vh::protocols::http::handler {

// Maps the typed failures of the HTTP lanes (access::*, storage::ContentUnavailable/IntegrityError,
// render::*, std::length_error) onto status codes with a JSON body {"error", "code"}. Unknown exceptions become a
// generic 500: internal details are logged, never returned.
[[nodiscard]] model::preview::Response errorFor(const request& req, const std::exception& e);
[[nodiscard]] model::preview::Response jsonError(const request& req, status code, std::string_view errorCode,
                                                 std::string_view message);

[[nodiscard]] std::string httpDate(std::time_t t);
// RFC 9110 If-None-Match: "*" or a list; weak comparison.
[[nodiscard]] bool noneMatchHits(std::string_view header, const std::string& etag);

// One representation served with full HTTP semantics: conditional GET (304), single Range (206/416), If-Range,
// HEAD, strong validators and streaming. `open` is called only when body bytes are actually needed (never for
// HEAD, 304 or 416), so a HEAD on a remote-only file never fetches it.
struct ServeSpec {
    uint64_t size{};
    std::string etag;
    std::time_t lastModified{};
    std::string contentType;
    std::string contentDisposition;          // empty: none
    std::string cacheControl{"private, no-transform"};
    std::vector<std::pair<std::string, std::string>> extraHeaders;
    bool capOpenEndedRanges{false};          // inline media: answer "bytes=N-" with at most 16 MiB
    std::function<std::shared_ptr<storage::PlaintextReader>()> open;
    // Called once for a GET that will send body bytes, with whether the response starts at byte 0. Return false to
    // refuse (403 max_downloads_reached).
    std::function<bool(bool startsAtZero)> admit;
    std::function<void(uint64_t sent, bool complete)> onFinish;
};

[[nodiscard]] model::preview::Response serve(const request& req, ServeSpec spec);

// "inline; filename=…" / "attachment; filename=…" with the RFC 6266/5987 encoding of Router::attachmentContentDisposition.
[[nodiscard]] std::string contentDisposition(bool inlineDisposition, const std::string& filename);

// Content-Type for original bytes: media types the browser should consume natively keep their MIME; everything
// else is served as application/octet-stream so a navigation never renders user-controlled HTML/XML/SVG as a
// document. Every original-bytes response also carries nosniff and a sandbox CSP.
[[nodiscard]] std::string safeContentType(const std::string& mime, bool inlineDisposition);
void addOriginalBytesHardening(ServeSpec& spec);

}
