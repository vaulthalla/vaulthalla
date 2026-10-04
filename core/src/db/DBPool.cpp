#include "db/DBPool.hpp"

#include "log/Registry.hpp"

namespace vh::db {

void DBPool::replaceBroken(Connection& conn) {
    {
        std::lock_guard lock(mtx_);
        if (std::chrono::steady_clock::now() < reconnectNotBefore_)
            throw DatabaseUnavailable(
                "Database connection lost; reconnect is backing off after " +
                std::to_string(consecutiveReconnectFailures_) + " failed attempt(s)");
    }

    try {
        conn.reconnect();
    } catch (const std::exception& e) {
        std::uint32_t failures = 0;
        {
            std::lock_guard lock(mtx_);
            ++reconnectFailures_;
            failures = ++consecutiveReconnectFailures_;
            const auto shift = std::min<std::uint32_t>(failures - 1, 5);
            const auto backoff = std::min(RECONNECT_BACKOFF_MAX, RECONNECT_BACKOFF_BASE * (1 << shift));
            reconnectNotBefore_ = std::chrono::steady_clock::now() + backoff;
        }
        log::Registry::db()->error("[DBPool] Reconnect failed ({} consecutive): {}", failures, e.what());
        throw DatabaseUnavailable(std::string("Database connection lost and reconnect failed: ") + e.what());
    }

    std::uint32_t recovered = 0;
    {
        std::lock_guard lock(mtx_);
        ++reconnects_;
        recovered = std::exchange(consecutiveReconnectFailures_, 0);
        reconnectNotBefore_ = {};
    }
    log::Registry::db()->warn("[DBPool] Replaced broken database connection (after {} failed attempt(s))",
                              recovered);
}

}
