#include "db/Janitor.hpp"
#include "config/Registry.hpp"
#include "db/query/sync/Event.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "log/Registry.hpp"
#include "preview/cache/Maintenance.hpp"
#include "share/Manager.hpp"

#include <algorithm>

using namespace vh::config;

namespace vh::db::janitor_impl {
// Derived preview artifacts are kept within caching.max_size_mb on a shorter cadence than the DB sweep.
constexpr std::chrono::minutes kDerivedCacheInterval{15};
}

vh::db::Janitor::Janitor()
    : AsyncService("DBSweeper"),
      sweep_interval_(Registry::get().services.db_sweeper.sweep_interval_minutes) {}

void vh::db::Janitor::runLoop() {
    auto nextDbSweep = std::chrono::steady_clock::now();
    while (!shouldStop()) {
        if (std::chrono::steady_clock::now() >= nextDbSweep) {
            try {
                query::sync::Event::purgeOld();
                query::auth::RefreshToken::purgeOldRevoked();
                const auto swept = vh::share::Manager{}.sweepStaleUploads();
                if (swept.failed > 0)
                    log::Registry::vaulthalla()->info("[DBSweeper] Marked {} stale share uploads failed", swept.failed);
            } catch (const std::exception& e) {
                log::Registry::vaulthalla()->warn("[DBSweeper] Failed to run database cleanup: {}", e.what());
            }
            nextDbSweep = std::chrono::steady_clock::now() + sweep_interval_;
        }

        try {
            (void)preview::cache::evictPeriodic();
        } catch (const std::exception& e) {
            log::Registry::vaulthalla()->warn("[DBSweeper] Derived artifact eviction failed: {}", e.what());
        }

        lazySleep(std::min<std::chrono::minutes>(sweep_interval_, janitor_impl::kDerivedCacheInterval));
    }
}
