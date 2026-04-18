#include "stats/model/CacheStats.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

using namespace vh::stats::model;

void LatencyStats::observe_us(uint64_t us) noexcept {
    count.v.fetch_add(1, std::memory_order_relaxed);
    total_us.v.fetch_add(us, std::memory_order_relaxed);

    uint64_t cur = max_us.v.load(std::memory_order_relaxed);
    while (us > cur && !max_us.v.compare_exchange_weak(cur, us, std::memory_order_relaxed)) {
        // cur updated by compare_exchange_weak
    }
}

void CacheStats::record_hit(uint64_t bytes) noexcept {
    hits.v.fetch_add(1, std::memory_order_relaxed);
    if (bytes) bytes_read.v.fetch_add(bytes, std::memory_order_relaxed);
}

void CacheStats::record_miss() noexcept {
    misses.v.fetch_add(1, std::memory_order_relaxed);
}

void CacheStats::record_insert(uint64_t bytes) noexcept {
    inserts.v.fetch_add(1, std::memory_order_relaxed);
    if (bytes) bytes_written.v.fetch_add(bytes, std::memory_order_relaxed);
}

void CacheStats::record_eviction() noexcept {
    evictions.v.fetch_add(1, std::memory_order_relaxed);
}

void CacheStats::record_invalidation() noexcept {
    invalidations.v.fetch_add(1, std::memory_order_relaxed);
}

void CacheStats::set_used(uint64_t used) noexcept {
    used_bytes.v.store(used, std::memory_order_relaxed);
}

void CacheStats::set_capacity(uint64_t cap) noexcept {
    capacity_bytes.v.store(cap, std::memory_order_relaxed);
}

void CacheStats::record_op_us(uint64_t us) noexcept {
    op_latency.observe_us(us);
}

CacheStatsSnapshot CacheStats::snapshot() const noexcept {
    CacheStatsSnapshot s;
    s.hits = hits.v.load(std::memory_order_relaxed);
    s.misses = misses.v.load(std::memory_order_relaxed);

    s.evictions = evictions.v.load(std::memory_order_relaxed);
    s.inserts = inserts.v.load(std::memory_order_relaxed);
    s.invalidations = invalidations.v.load(std::memory_order_relaxed);

    s.bytes_read = bytes_read.v.load(std::memory_order_relaxed);
    s.bytes_written = bytes_written.v.load(std::memory_order_relaxed);

    s.used_bytes = used_bytes.v.load(std::memory_order_relaxed);
    s.capacity_bytes = capacity_bytes.v.load(std::memory_order_relaxed);

    s.op_count = op_latency.count.v.load(std::memory_order_relaxed);
    s.op_total_us = op_latency.total_us.v.load(std::memory_order_relaxed);
    s.op_max_us = op_latency.max_us.v.load(std::memory_order_relaxed);

    return s;
}

double CacheStats::hit_rate(const CacheStatsSnapshot& s) noexcept {
    const auto denom = s.hits + s.misses;
    return denom ? static_cast<double>(s.hits) / static_cast<double>(denom) : 0.0;
}

uint64_t CacheStats::free_bytes(const CacheStatsSnapshot& s) noexcept {
    return (s.capacity_bytes > s.used_bytes) ? (s.capacity_bytes - s.used_bytes) : 0;
}

double CacheStats::avg_op_ms(const CacheStatsSnapshot& s) noexcept {
    return s.op_count ? (static_cast<double>(s.op_total_us) / 1000.0) / static_cast<double>(s.op_count) : 0.0;
}

double CacheStats::max_op_ms(const CacheStatsSnapshot& s) noexcept {
    return static_cast<double>(s.op_max_us) / 1000.0;
}

// Snapshot serialization (best practice)
void vh::stats::model::to_json(nlohmann::json& j, const CacheStatsSnapshot& s) {
    j = nlohmann::json{
        {"hits", s.hits},
        {"misses", s.misses},
        {"evictions", s.evictions},
        {"inserts", s.inserts},
        {"invalidations", s.invalidations},

        {"bytes_read", s.bytes_read},
        {"bytes_written", s.bytes_written},

        {"used_bytes", s.used_bytes},
        {"capacity_bytes", s.capacity_bytes},

        {"op", {
            {"count", s.op_count},
            {"total_us", s.op_total_us},
            {"max_us", s.op_max_us},
            {"avg_ms", CacheStats::avg_op_ms(s)},
            {"max_ms", CacheStats::max_op_ms(s)},
        }},
    };
}

void vh::stats::model::to_json(nlohmann::json& j, const std::shared_ptr<CacheStatsSnapshot>& s) {
    j = *s;
}

// Convenience serialization (snapshot + derived fields)
void vh::stats::model::to_json(nlohmann::json& j, const CacheStats& s) {
    const auto snap = s.snapshot();

    j = nlohmann::json{
        {"hits", snap.hits},
        {"misses", snap.misses},
        {"evictions", snap.evictions},
        {"inserts", snap.inserts},
        {"invalidations", snap.invalidations},

        {"bytes_read", snap.bytes_read},
        {"bytes_written", snap.bytes_written},

        {"used_bytes", snap.used_bytes},
        {"free_bytes", CacheStats::free_bytes(snap)},
        {"capacity_bytes", snap.capacity_bytes},

        {"hit_rate", CacheStats::hit_rate(snap)},

        {"op", {
            {"count", snap.op_count},
            {"total_us", snap.op_total_us},
            {"max_us", snap.op_max_us},
            {"avg_ms", CacheStats::avg_op_ms(snap)},
            {"max_ms", CacheStats::max_op_ms(snap)},
        }},
    };
}

void vh::stats::model::to_json(nlohmann::json& j, const std::shared_ptr<CacheStats>& s) {
    j = *s;
}
