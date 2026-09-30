#pragma once

#include "DBPool.hpp"
#include "log/Registry.hpp"

#include <memory>
#include <optional>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace vh::db {

    class Transactions {
    public:
        static inline std::shared_ptr<DBPool> dbPool_;

        static void init() { dbPool_ = std::make_shared<DBPool>(); }

        template <typename Func>
        static decltype(auto) exec(const std::string& ctx, Func&& func) {
            using Fn = std::remove_reference_t<Func>;
            using ReturnT = std::invoke_result_t<Fn&, pqxx::work&>;

            // Local copy keeps the pool alive for as long as this call holds a lease on it.
            const auto pool = dbPool_;
            if (!pool) throw std::runtime_error("Transactions not initialized!");

            log::Registry::db()->trace("[Transactions::exec] Starting transaction: {}", ctx);

            // Declared before `txn` so the transaction is torn down (rolled back) before the lease hands the
            // connection back to the pool, on every path.
            auto conn = pool->acquire();
            std::optional<pqxx::work> txn;

            try {
                txn.emplace(conn->get());
            } catch (const std::exception& e) {
                // A session the server dropped while idle (PostgreSQL restart, pg_terminate_backend) only shows
                // up when BEGIN is sent. Nothing of `func` has run yet, so replacing the connection and beginning
                // once more is always safe. Failures after `func` starts are surfaced, never retried.
                if (conn->healthy()) {
                    log::Registry::db()->error(
                        "[Transactions::exec] Failed to begin transaction '{}': {}", ctx, e.what());
                    throw;
                }
                log::Registry::db()->warn(
                    "[Transactions::exec] Connection lost before transaction '{}' began ({}); reconnecting",
                    ctx, e.what());
                pool->repair(conn);
                txn.emplace(conn->get());
            }

            try {
                if constexpr (std::is_void_v<ReturnT>) {
                    func(*txn);
                    txn->commit();
                    log::Registry::db()->trace("[Transactions::exec] Transaction committed: {}", ctx);
                    return;
                } else {
                    ReturnT result = func(*txn);
                    txn->commit();
                    log::Registry::db()->trace("[Transactions::exec] Transaction committed: {}", ctx);
                    return result;
                }
            } catch (...) {
                log::Registry::db()->error(
                    "[Transactions::exec] Exception in transaction context '{}', rolling back",
                    ctx
                );
                throw;
            }
        }
    };

}
