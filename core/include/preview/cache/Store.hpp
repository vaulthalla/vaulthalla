#pragma once

#include "storage/Fwd.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace vh::storage {
    class PlaintextReader;
}

namespace vh::preview::cache {

// Identity of one derived artifact. source_id is the source file's Generation::sourceId(): it changes on every
// content write, so a content change invalidates every artifact by construction.
struct ArtifactKey {
    uint32_t vault_id{};
    uint32_t file_id{};
    std::string kind;       // "thumbnail", "render", "model-glb", "poster-jpg", "probe-json", "transcode-…"
    std::string variant;    // "128", "p0-s1024", "v1", …
    std::string source_id;
    uint32_t generator_version{1};

    // "<vault_id>/<file_id>/<kind>/<variant>/<source_id>/<generator_version>": bound into the artifact's GCM AAD.
    [[nodiscard]] std::string canonical() const;
};

struct Artifact {
    ArtifactKey key;
    std::filesystem::path path;  // absolute
    uint64_t size{};              // plaintext size
};

enum class LookupStatus { Ready, Missing, Failed };

struct Lookup {
    LookupStatus status{LookupStatus::Missing};
    std::optional<Artifact> artifact;
    std::string failure;
};

// One encrypted, non-authoritative cache for every derived artifact (thumbnails, page renders, posters, GLB,
// transcodes). Files live under <cacheRoot>/derived/<alias>/, are AES-256-GCM sealed with the vault key under a
// fresh IV ("VHDERIV1" header, identity bound as AAD), indexed in cache_index (type 'derived'), replaced atomically,
// evicted LRU to caching.max_size_mb and regenerated on demand. Never synced, never visible over FUSE.
class Store {
public:
    static constexpr std::size_t kHeaderSize = 64;

    [[nodiscard]] static Lookup lookup(const std::shared_ptr<storage::Engine>& engine, const ArtifactKey& key);

    [[nodiscard]] static std::unique_ptr<storage::PlaintextReader> open(
        const std::shared_ptr<storage::Engine>& engine, const Artifact& artifact);

    // Streaming writer: plaintext in, sealed temp file out; commit() fsyncs, renames into place and indexes.
    class Writer {
    public:
        Writer(Writer&&) noexcept;
        Writer& operator=(Writer&&) noexcept;
        ~Writer();  // aborts (removes the temp file) unless committed

        void write(std::span<const uint8_t> plaintext);
        Artifact commit();
        void abort() noexcept;

        [[nodiscard]] uint64_t bytesWritten() const;

    private:
        friend class Store;
        struct Impl;
        explicit Writer(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };

    [[nodiscard]] static Writer begin(const std::shared_ptr<storage::Engine>& engine, const ArtifactKey& key,
                                      uint64_t maxBytes);

    static Artifact put(const std::shared_ptr<storage::Engine>& engine, const ArtifactKey& key,
                        std::span<const uint8_t> plaintext);

    // Negative cache: the generator failed for this exact key; lookups return Failed until the TTL passes or the
    // source changes.
    static void putFailure(const std::shared_ptr<storage::Engine>& engine, const ArtifactKey& key,
                           const std::string& reason);

    static void purgeFile(const std::shared_ptr<storage::Engine>& engine, uint32_t fileId);
    static void purgeVault(const std::shared_ptr<storage::Engine>& engine);

    // Evicts least-recently-used artifacts (all vaults) until the total is within maxBytes, and drops artifacts
    // unused for longer than maxIdle. Returns bytes freed.
    static uint64_t evict(uint64_t maxBytes, std::optional<std::chrono::seconds> maxIdle = std::nullopt);
};

}
