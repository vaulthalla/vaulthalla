#pragma once

#include "DBPool.hpp"
#include "log/Registry.hpp"

#include <memory>
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

            if (!dbPool_) throw std::runtime_error("Transactions not initialized!");

            log::Registry::db()->trace("[Transactions::exec] Starting transaction: {}", ctx);

            auto conn = dbPool_->acquire();
            pqxx::work txn(conn->get());

            try {
                if constexpr (std::is_void_v<ReturnT>) {
                    func(txn);
                    txn.commit();
                    log::Registry::db()->trace("[Transactions::exec] Transaction committed: {}", ctx);
                    dbPool_->release(std::move(conn));
                    return;
                } else {
                    ReturnT result = func(txn);
                    txn.commit();
                    log::Registry::db()->trace("[Transactions::exec] Transaction committed: {}", ctx);
                    dbPool_->release(std::move(conn));
                    return result;
                }
            } catch (...) {
                log::Registry::db()->error(
                    "[Transactions::exec] Exception in transaction context '{}', rolling back",
                    ctx
                );
                dbPool_->release(std::move(conn));
                throw;
            }
        }
    };

}
