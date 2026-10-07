#include "preview/render/Service.hpp"

#include "concurrency/Task.hpp"
#include "concurrency/ThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "config/Registry.hpp"
#include "fs/model/File.hpp"
#include "log/Registry.hpp"
#include "preview/Plan.hpp"
#include "preview/cache/Store.hpp"
#include "preview/render/Raster.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/CacheStats.hpp"
#include "storage/Engine.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <unordered_set>

namespace vh::preview::render {

namespace {

constexpr uint32_t kGeneratorVersion = 2;  // 1 = the legacy plaintext pipeline
constexpr uint64_t kKeepUploadBufferBytes = 64ull * 1024 * 1024;

[[nodiscard]] std::counting_semaphore<>& renderGate() {
    static std::counting_semaphore<> gate(
        static_cast<std::ptrdiff_t>(std::clamp(std::thread::hardware_concurrency() / 2, 2u, 8u)));
    return gate;
}

struct GateSlot {
    GateSlot() {
        if (!renderGate().try_acquire_for(std::chrono::seconds(20)))
            throw Busy("All preview render slots are busy");
    }
    ~GateSlot() { renderGate().release(); }
    GateSlot(const GateSlot&) = delete;
    GateSlot& operator=(const GateSlot&) = delete;
};

[[nodiscard]] bool isPdf(const fs::model::File& file) {
    return classify(file).renderer == "pdf";
}

[[nodiscard]] bool isThumbnailSize(const uint32_t size) {
    const auto& sizes = config::Registry::get().caching.thumbnails.sizes;
    return std::ranges::find(sizes, size) != sizes.end();
}

[[nodiscard]] cache::ArtifactKey keyFor(const std::shared_ptr<storage::Engine>& engine, const fs::model::File& file,
                                        const Request& request) {
    auto generation = storage::generationOf(file);
    cache::ArtifactKey key{
        .vault_id = engine && engine->vault ? engine->vault->id : generation.vault_id,
        .file_id = file.id,
        .kind = "render",
        .variant = "p" + std::to_string(request.page) + "-s" + std::to_string(request.size),
        .source_id = generation.sourceId(),
        .generator_version = kGeneratorVersion
    };
    if (request.page == 0 && isThumbnailSize(request.size)) {
        key.kind = "thumbnail";
        key.variant = std::to_string(request.size);
    }
    return key;
}

[[nodiscard]] cache::ArtifactKey pdfInfoKey(const std::shared_ptr<storage::Engine>& engine, const fs::model::File& file) {
    auto key = keyFor(engine, file, {.size = 0, .page = 0});
    key.kind = "pdf-info";
    key.variant = "v1";
    return key;
}

// The derived cache is an optimization: when it can't be reached (database unavailable, no vault key loaded)
// renders still work, they are just not cached.
[[nodiscard]] cache::Lookup safeLookup(const std::shared_ptr<storage::Engine>& engine, const cache::ArtifactKey& key) {
    try {
        return cache::Store::lookup(engine, key);
    } catch (const std::exception& e) {
        log::Registry::thumb()->debug("[Render] Derived cache unavailable for {}: {}", key.canonical(), e.what());
        return {};
    }
}

[[nodiscard]] std::optional<std::vector<uint8_t>> readArtifact(const std::shared_ptr<storage::Engine>& engine,
                                                               const cache::ArtifactKey& key) {
    const auto lookup = safeLookup(engine, key);
    if (lookup.status != cache::LookupStatus::Ready || !lookup.artifact) return std::nullopt;
    try {
        const auto reader = cache::Store::open(engine, *lookup.artifact);
        return storage::readAll(*reader, 64ull * 1024 * 1024);
    } catch (const std::exception& e) {
        log::Registry::thumb()->warn("[Render] Dropping unreadable cached artifact {}: {}", key.canonical(), e.what());
        try {
            cache::Store::purgeFile(engine, key.file_id);
        } catch (...) {}
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<uint32_t> cachedPageCount(const std::shared_ptr<storage::Engine>& engine,
                                                      const fs::model::File& file) {
    const auto bytes = readArtifact(engine, pdfInfoKey(engine, file));
    if (!bytes) return std::nullopt;
    try {
        return static_cast<uint32_t>(std::stoul(std::string(bytes->begin(), bytes->end())));
    } catch (...) {
        return std::nullopt;
    }
}

void storeQuietly(const std::shared_ptr<storage::Engine>& engine, const cache::ArtifactKey& key,
                  const std::vector<uint8_t>& bytes) {
    try {
        (void)cache::Store::put(engine, key, bytes);
    } catch (const std::exception& e) {
        log::Registry::thumb()->warn("[Render] Could not cache {}: {}", key.canonical(), e.what());
    }
}

[[nodiscard]] std::vector<uint8_t> sourceBytes(const std::shared_ptr<storage::Engine>& engine,
                                               const std::shared_ptr<fs::model::File>& file,
                                               const storage::RemoteFetchPolicy remote, const Limits& limits) {
    if (file->size_bytes > limits.maxSourceBytes) throw LimitExceeded("File exceeds the preview size limit");
    const auto reader = engine->openPlaintextReader(file, {.integrity = storage::IntegrityPolicy::FromConfig,
                                                           .remote = remote});
    return storage::readAll(*reader, limits.maxSourceBytes);
}

[[nodiscard]] Raster rasterize(const fs::model::File& file, const std::span<const uint8_t> bytes, const uint32_t size,
                               const uint32_t page, const Limits& limits, std::optional<uint32_t>& pageCount) {
    if (isPdf(file)) {
        uint32_t pages = 0;
        auto raster = renderPdfPage(bytes, page, size, limits, pages);
        pageCount = pages;
        return raster;
    }
    if (page != 0) throw InvalidInput("Images have a single page");
    return decodeImage(bytes, size, limits);
}

std::mutex& inflightMutex() {
    static std::mutex m;
    return m;
}

std::unordered_set<std::string>& inflight() {
    static std::unordered_set<std::string> s;
    return s;
}

struct ThumbnailTask final : concurrency::Task {
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<fs::model::File> file;
    std::vector<uint8_t> plaintext;
    std::string dedupeKey;

    void operator()() override {
        try {
            generateThumbnails(engine, file, plaintext);
        } catch (const std::exception& e) {
            log::Registry::thumb()->debug("[Render] Thumbnail generation skipped for file {}: {}", file->id, e.what());
        }
        std::fill(plaintext.begin(), plaintext.end(), uint8_t{0});
        std::scoped_lock lock(inflightMutex());
        inflight().erase(dedupeKey);
    }
};

}

uint32_t normalizeSize(const uint32_t requested) {
    if (requested == 0) return 1024;
    return std::clamp<uint32_t>(requested, 16, 2048);
}

std::optional<Result> cached(const std::shared_ptr<storage::Engine>& engine,
                             const std::shared_ptr<fs::model::File>& file, Request request) {
    if (!engine || !file) return std::nullopt;
    request.size = normalizeSize(request.size);
    const auto bytes = readArtifact(engine, keyFor(engine, *file, request));
    if (!bytes) {
        if (const auto& stats = runtime::Deps::get().httpCacheStats) stats->record_miss();
        return std::nullopt;
    }
    if (const auto& stats = runtime::Deps::get().httpCacheStats) stats->record_hit(bytes->size());
    Result result{.jpeg = *bytes, .cacheHit = true, .pageCount = std::nullopt};
    if (isPdf(*file)) result.pageCount = cachedPageCount(engine, *file);
    return result;
}

Result render(const std::shared_ptr<storage::Engine>& engine, const std::shared_ptr<fs::model::File>& liveFile,
              Request request, const storage::RemoteFetchPolicy remote) {
    if (!engine || !liveFile) throw std::invalid_argument("Nothing to render");
    // One immutable snapshot for the whole render: the cache key and the reader must describe the same generation.
    // (Overwrites mutate the cached File in place; a key taken from it after reading could label old pixels as new.)
    const auto file = std::make_shared<fs::model::File>(*liveFile);
    if (classify(*file).kind != PreviewKind::RenderedImage) throw InvalidInput("This file type has no server render");
    request.size = normalizeSize(request.size);
    if (auto hit = cached(engine, file, request); hit && (!isPdf(*file) || hit->pageCount)) return std::move(*hit);

    const auto started = std::chrono::steady_clock::now();
    const auto limits = limitsFromConfig();
    GateSlot slot;
    const auto bytes = sourceBytes(engine, file, remote, limits);
    std::optional<uint32_t> pageCount;
    const auto raster = fit(rasterize(*file, bytes, request.size, request.page, limits, pageCount), request.size);
    Result result{.jpeg = encodeJpeg(raster), .cacheHit = false, .pageCount = pageCount};

    storeQuietly(engine, keyFor(engine, *file, request), result.jpeg);
    if (pageCount) {
        const auto text = std::to_string(*pageCount);
        storeQuietly(engine, pdfInfoKey(engine, *file), std::vector<uint8_t>(text.begin(), text.end()));
    }
    if (const auto& stats = runtime::Deps::get().httpCacheStats)
        stats->record_op_us(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count()));
    return result;
}

void generateThumbnails(const std::shared_ptr<storage::Engine>& engine, const std::shared_ptr<fs::model::File>& liveFile,
                        const std::span<const uint8_t> plaintext) {
    if (!engine || !liveFile || liveFile->size_bytes == 0) return;
    const auto file = std::make_shared<fs::model::File>(*liveFile);  // see render(): key and bytes from one generation
    const auto plan = classify(*file);
    if (!plan.thumbnail) return;

    auto sizes = config::Registry::get().caching.thumbnails.sizes;
    std::erase_if(sizes, [](const unsigned int s) { return s < 16 || s > 2048; });
    if (sizes.empty()) return;
    std::ranges::sort(sizes, std::greater<>());
    std::erase_if(sizes, [&](const unsigned int s) {
        return safeLookup(engine, keyFor(engine, *file, {.size = s, .page = 0})).status == cache::LookupStatus::Ready;
    });
    if (sizes.empty()) return;

    const auto limits = limitsFromConfig();
    GateSlot slot;
    std::vector<uint8_t> owned;
    std::span<const uint8_t> bytes = plaintext;
    if (bytes.empty() || bytes.size() != file->size_bytes) {
        owned = sourceBytes(engine, file, storage::RemoteFetchPolicy::Off, limits);
        bytes = owned;
    }

    // One decode at the largest size, then each smaller size is resized from the previous one.
    std::optional<uint32_t> pageCount;
    auto current = fit(rasterize(*file, bytes, sizes.front(), 0, limits, pageCount), sizes.front());
    for (const auto size : sizes) {
        current = fit(current, size);
        storeQuietly(engine, keyFor(engine, *file, {.size = size, .page = 0}), encodeJpeg(current));
    }
    if (pageCount) {
        const auto text = std::to_string(*pageCount);
        storeQuietly(engine, pdfInfoKey(engine, *file), std::vector<uint8_t>(text.begin(), text.end()));
    }
    std::fill(owned.begin(), owned.end(), uint8_t{0});
}

void enqueueThumbnails(const std::shared_ptr<storage::Engine>& engine, const std::shared_ptr<fs::model::File>& file,
                       const std::span<const uint8_t> plaintext) {
    try {
        if (!engine || !file || !classify(*file).thumbnail || file->size_bytes == 0) return;
        const auto key = std::to_string(file->id) + ":" + storage::generationOf(*file).sourceId();
        {
            std::scoped_lock lock(inflightMutex());
            if (!inflight().insert(key).second) return;
        }
        auto task = std::make_shared<ThumbnailTask>();
        task->engine = engine;
        // Snapshot now: the plaintext handed in belongs to this generation, and the file may be overwritten before
        // the task runs (thumbnails of old bytes must never be stored under a newer source id).
        task->file = std::make_shared<fs::model::File>(*file);
        task->dedupeKey = key;
        if (!plaintext.empty() && plaintext.size() == file->size_bytes && plaintext.size() <= kKeepUploadBufferBytes)
            task->plaintext.assign(plaintext.begin(), plaintext.end());
        const auto pool = concurrency::ThreadPoolManager::instance().thumbPool();
        if (!pool) {
            std::scoped_lock lock(inflightMutex());
            inflight().erase(key);
            return;
        }
        pool->submit(task);
    } catch (const std::exception& e) {
        log::Registry::thumb()->error("[Render] Failed to enqueue thumbnails: {}", e.what());
    }
}

}
