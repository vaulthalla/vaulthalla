#pragma once

#include "storage/Fwd.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Daemon-lifecycle hooks around the derived-artifact store (preview/cache/Store.hpp), called from main (startup)
// and db::Janitor (periodic), and kept here so they are testable without the daemon.
namespace vh::preview::cache {

// Process-wide preview settings from config: PlaintextReader default policies (preview.media.integrity/remote)
// and the negative-cache TTL (preview.derive.failure_ttl_hours). Startup, after the config is loaded.
void applyConfig();

// Startup, once storage engines are loaded: Store::sweep for every vault (legacy plaintext thumbnails, artifact
// directories of deleted files, interrupted temp writes). Never throws; logs what it removed. Returns the count.
std::size_t sweepAtStartup(const std::vector<std::shared_ptr<storage::Engine>>& engines);

// Periodic (db::Janitor): evict least-recently-used artifacts down to caching.max_size_mb and drop artifacts unused
// for caching.thumbnails.expiry_days. Updates the cache stats. Returns bytes freed.
uint64_t evictPeriodic();

}
