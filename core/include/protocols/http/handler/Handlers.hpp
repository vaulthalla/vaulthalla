#pragma once

#include "protocols/http/Router.hpp"

namespace vh::protocols::http::handler {

// GET|HEAD /download            files: streamed original bytes (attachment, Range-capable); dirs: streamed STORE ZIP
// GET|HEAD /download/content    files only; disposition=inline|attachment (default inline)
[[nodiscard]] model::preview::Response content(request&& req);

// GET|HEAD /preview             lossy JPEG render of RenderedImage files (image, PDF page); never original bytes
[[nodiscard]] model::preview::Response preview(request&& req);

// POST /preview/batch           thumbnail readiness for a page of rows
[[nodiscard]] model::preview::Response previewBatch(request&& req);

// GET|HEAD /preview/derived     derived artifacts (GLB from STEP, posters, probes, transcodes)
[[nodiscard]] model::preview::Response derived(request&& req);

// GET|HEAD /download/conflict   one side of an open sync conflict (#187): conflict_id, side=local|remote. Needs
//                               resolve_conflicts + filesystem Read; the remote side is fetched on demand
//                               (price-preflighted, metered, at most kConflictPreviewMaxBytes, never stored).
inline constexpr uint64_t kConflictPreviewMaxBytes = 32ull * 1024 * 1024;
[[nodiscard]] model::preview::Response conflictSide(request&& req);

// PUT /upload/text              save a text document with optimistic concurrency (If-Match)
[[nodiscard]] model::preview::Response saveText(request&& req);

}
