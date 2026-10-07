#pragma once

#include "fs/Fwd.hpp"
#include "preview/cache/Store.hpp"
#include "storage/Fwd.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace vh::preview::derive {

// Kinds produced by out-of-process helpers:
//   model-glb                    vaulthalla-preview-cad   convert-step     (STEP/STP -> GLB)
//   poster-jpg                   vaulthalla-preview-media poster           (video poster / audio cover art)
//   probe-json                   vaulthalla-preview-media probe            (codec facts, browser direct-play)
//   transcode-h264-{480,720,1080} vaulthalla-preview-media transcode       (fragmented MP4, H.264/AAC)
enum class DeriveStatus {
    Ready,        // artifact available
    Queued,       // accepted (or already running); poll again after retryAfterSeconds
    Failed,       // negatively cached failure for this exact source generation
    Unavailable,  // helper package not installed (HTTP 503 converter_unavailable)
    Unsupported,  // kind not applicable to this file (HTTP 415)
    Busy          // queue full (HTTP 503 + Retry-After)
};

struct DeriveResult {
    DeriveStatus status{DeriveStatus::Unsupported};
    std::optional<cache::Artifact> artifact;
    std::string reason;          // failure reason (Failed/Unavailable/Unsupported)
    std::string helper;          // helper binary name for Unavailable
    uint32_t retryAfterSeconds{2};
};

[[nodiscard]] bool kindKnown(std::string_view kind);
[[nodiscard]] uint32_t generatorVersion(std::string_view kind);
[[nodiscard]] std::string_view helperFor(std::string_view kind);

// Bounded, deduplicating job queue in front of derive::Runner and cache::Store. request() never blocks on
// conversion: it returns Ready (cache hit), Failed (negative cache), or schedules one job per ArtifactKey and
// returns Queued. Jobs stream the source through a PlaintextReader into the helper and the helper's output into
// a Store::Writer; failures become negative-cache entries.
class Queue {
public:
    static Queue& instance();

    DeriveResult request(const std::shared_ptr<storage::Engine>& engine,
                         const std::shared_ptr<fs::model::File>& file,
                         const std::string& kind,
                         const std::string& variant = "v1");

    struct Stats {
        uint64_t queued{}, running{}, completed{}, failed{}, rejectedBusy{};
    };
    [[nodiscard]] Stats stats() const;

    // Stops accepting work, kills running helpers' process groups via Runner timeouts, joins workers.
    void shutdown();

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

private:
    Queue();
    ~Queue();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
