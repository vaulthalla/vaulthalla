#include "protocols/ws/handler/Stats.hpp"

#include "concurrency/ThreadPoolManager.hpp"
#include "concurrency/ThreadPool.hpp"
#include "nlohmann/json.hpp"
#include "protocols/ws/Session.hpp"
#include "vault/task/Stats.hpp"
#include "vault/model/Stat.hpp"
#include "identities/User.hpp"
#include "stats/model/CacheStats.hpp"
#include "runtime/Deps.hpp"
#include "fs/cache/Registry.hpp"
#include "rbac/resolver/admin/all.hpp"

#include <future>
#include <utility>
#include <array>

using namespace vh::stats;
using namespace vh::rbac;

namespace vh::protocols::ws::handler {

json Stats::vault(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view stats for this vault.");

    const auto task = std::make_shared<vault::task::Stats>(vaultId);
    auto future = task->getFuture().value();
    concurrency::ThreadPoolManager::instance().statsPool()->submit(task);

    if (const auto stats = std::get<std::shared_ptr<vault::model::Stat>>(future.get()))
        return {{"stats", stats}};

    throw std::runtime_error("Unable to load vault stats");
}

json Stats::fsCache(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view cache stats.");
    const auto stats = runtime::Deps::get().fsCache->stats();
    if (!stats) throw std::runtime_error("No cache stats available.");
    return {{"stats", stats}};
}

json Stats::httpCache(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view cache stats.");
    return {{"stats", runtime::Deps::get().httpCacheStats->snapshot()}};
}

}
