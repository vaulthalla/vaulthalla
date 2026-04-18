#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

// 64-byte cache line padding helper to avoid false sharing.
// (Most x86_64 is 64B; if you ever want, we can make this configurable.)
constexpr std::size_t kCacheLine = 64;

template <typename T>
struct alignas(kCacheLine) PaddedAtomic {
    std::atomic<T> v{0};
    // pad out the remainder of the cache line (best-effort)
    char pad[kCacheLine - (sizeof(std::atomic<T>) % kCacheLine ? (sizeof(std::atomic<T>) % kCacheLine) : kCacheLine)]{};
};

struct LatencyStats {
    PaddedAtomic<uint64_t> count;
    PaddedAtomic<uint64_t> total_us;
    PaddedAtomic<uint64_t> max_us;

    void observe_us(uint64_t us) noexcept;
};

struct CacheStatsSnapshot {
    uint64_t hits{};
    uint64_t misses{};

    uint64_t evictions{};
    uint64_t inserts{};
    uint64_t invalidations{};

    uint64_t bytes_read{};
    uint64_t bytes_written{};

    uint64_t used_bytes{};
    uint64_t capacity_bytes{};

    // Optional "work behind misses" (e.g., thumbnail generation time)
    uint64_t op_count{};
    uint64_t op_total_us{};
    uint64_t op_max_us{};
};

struct CacheStats {
    // Group “hot” counters into separate cache lines to reduce contention.
    PaddedAtomic<uint64_t> hits;
    PaddedAtomic<uint64_t> misses;

    PaddedAtomic<uint64_t> evictions;
    PaddedAtomic<uint64_t> inserts;
    PaddedAtomic<uint64_t> invalidations;

    PaddedAtomic<uint64_t> bytes_read;
    PaddedAtomic<uint64_t> bytes_written;

    // These are often “written rarely / read often”, still keep separated.
    PaddedAtomic<uint64_t> used_bytes;
    PaddedAtomic<uint64_t> capacity_bytes;

    LatencyStats op_latency;

    // Increment helpers
    void record_hit(uint64_t bytes = 0) noexcept;
    void record_miss() noexcept;
    void record_insert(uint64_t bytes = 0) noexcept;
    void record_eviction() noexcept;
    void record_invalidation() noexcept;

    void set_used(uint64_t used) noexcept;
    void set_capacity(uint64_t cap) noexcept;

    void record_op_us(uint64_t us) noexcept;

    [[nodiscard]] CacheStatsSnapshot snapshot() const noexcept;

    // Derivations should use snapshot values (not atomics)
    static double hit_rate(const CacheStatsSnapshot& s) noexcept;
    static uint64_t free_bytes(const CacheStatsSnapshot& s) noexcept;
    static double avg_op_ms(const CacheStatsSnapshot& s) noexcept;
    static double max_op_ms(const CacheStatsSnapshot& s) noexcept;
};

struct ScopedOpTimer {
    CacheStats* stats;
    std::chrono::steady_clock::time_point start{};
    explicit ScopedOpTimer(CacheStats* s)
        : stats(s), start(std::chrono::steady_clock::now()) {}
    ~ScopedOpTimer() {
        const auto end = std::chrono::steady_clock::now();
        stats->record_op_us(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }
};

// JSON serialization
void to_json(nlohmann::json& j, const std::shared_ptr<CacheStatsSnapshot>& s);
void to_json(nlohmann::json& j, const CacheStatsSnapshot& s);

// Optional convenience (serializes snapshot + derived fields)
void to_json(nlohmann::json& j, const std::shared_ptr<CacheStats>& s);
void to_json(nlohmann::json& j, const CacheStats& s);

}
