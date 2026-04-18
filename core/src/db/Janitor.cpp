#include "db/Janitor.hpp"
#include "config/Registry.hpp"
#include "db/query/sync/Event.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "log/Registry.hpp"

using namespace vh::config;

vh::db::Janitor::Janitor()
    : AsyncService("DBSweeper"),
      sweep_interval_(Registry::get().services.db_sweeper.sweep_interval_minutes) {}

void vh::db::Janitor::runLoop() {
    while (!shouldStop()) {
        try {
            query::sync::Event::purgeOld();
            query::auth::RefreshToken::purgeOldRevoked();
        } catch (const std::exception& e) {
            log::Registry::vaulthalla()->warn("[DBSweeper] Failed to purge old sync events: {}", e.what());
        }

        lazySleep(sweep_interval_);
    }
}
