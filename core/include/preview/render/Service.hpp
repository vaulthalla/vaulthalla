#pragma once

#include "fs/Fwd.hpp"
#include "storage/Fwd.hpp"
#include "storage/PlaintextReader.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

// Cached lossy renders (JPEG) of RenderedImage files: grid thumbnails (caching.thumbnails.sizes, kind
// "thumbnail") and sheet/page renders (kind "render", variant "p<page>-s<size>"), all stored encrypted in
// preview::cache::Store and keyed to the source generation. CPU work is bounded by a process-wide render gate.
namespace vh::preview::render {

struct Request {
    uint32_t size{1024};
    uint32_t page{0};
};

struct Result {
    std::vector<uint8_t> jpeg;
    bool cacheHit{false};
    std::optional<uint32_t> pageCount;  // PDFs
};

// All render slots stayed busy past the wait budget (HTTP 503 + Retry-After).
class Busy final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] uint32_t normalizeSize(uint32_t requested);

// Cached render, rendering on a miss. `remote` controls whether a cloud file without local bytes may be fetched
// (a user opening one file: FromConfig/hydrate; grid thumbnails: Off).
[[nodiscard]] Result render(const std::shared_ptr<storage::Engine>& engine,
                            const std::shared_ptr<fs::model::File>& file,
                            Request request,
                            storage::RemoteFetchPolicy remote = storage::RemoteFetchPolicy::FromConfig);

// Cache-only lookup (never renders).
[[nodiscard]] std::optional<Result> cached(const std::shared_ptr<storage::Engine>& engine,
                                           const std::shared_ptr<fs::model::File>& file,
                                           Request request);

// Renders every configured thumbnail size from ONE decode, downscaling along a chain (largest first).
// `plaintext`, when the caller already holds the bytes (an upload), avoids re-reading the file.
void generateThumbnails(const std::shared_ptr<storage::Engine>& engine,
                        const std::shared_ptr<fs::model::File>& file,
                        std::span<const uint8_t> plaintext = {});

// Schedules generateThumbnails on the thumbnail pool (deduplicated per file generation). Copies `plaintext`
// only when it is small enough to be worth keeping instead of re-reading.
void enqueueThumbnails(const std::shared_ptr<storage::Engine>& engine,
                       const std::shared_ptr<fs::model::File>& file,
                       std::span<const uint8_t> plaintext = {});

}
