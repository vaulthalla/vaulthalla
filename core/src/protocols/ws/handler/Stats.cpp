#include "protocols/ws/handler/Stats.hpp"

#include "concurrency/ThreadPoolManager.hpp"
#include "concurrency/ThreadPool.hpp"
#include "db/query/stats/DbStats.hpp"
#include "db/query/stats/OperationStats.hpp"
#include "db/query/stats/RetentionStats.hpp"
#include "db/query/stats/Snapshot.hpp"
#include "db/query/share/Stats.hpp"
#include "db/query/sync/Stats.hpp"
#include "db/query/vault/Activity.hpp"
#include "db/query/vault/Recovery.hpp"
#include "db/query/vault/Security.hpp"
#include "nlohmann/json.hpp"
#include "protocols/ws/Session.hpp"
#include "vault/task/Stats.hpp"
#include "vault/model/Stat.hpp"
#include "identities/User.hpp"
#include "stats/model/CacheStats.hpp"
#include "stats/model/ConnectionStats.hpp"
#include "stats/model/DashboardOverview.hpp"
#include "stats/model/DbStats.hpp"
#include "stats/model/FuseStats.hpp"
#include "stats/model/OperationStats.hpp"
#include "stats/model/RetentionStats.hpp"
#include "stats/model/StorageBackendStats.hpp"
#include "stats/model/StatsTrends.hpp"
#include "stats/model/SystemHealth.hpp"
#include "stats/model/ThreadPoolStats.hpp"
#include "stats/model/VaultActivity.hpp"
#include "stats/model/VaultRecovery.hpp"
#include "stats/model/VaultSecurity.hpp"
#include "stats/model/VaultShareStats.hpp"
#include "stats/model/VaultSyncHealth.hpp"
#include "runtime/Deps.hpp"
#include "fs/cache/Registry.hpp"
#include "rbac/resolver/admin/all.hpp"

#include <future>
#include <utility>
#include <array>
#include <algorithm>

using namespace vh::stats;
using namespace vh::rbac;

namespace vh::protocols::ws::handler {

namespace {

std::uint32_t statsTrendWindowHours(const json& payload) {
    if (!payload.is_object()) return 168;
    const auto raw = payload.value("window_hours", 168u);
    return std::clamp<std::uint32_t>(raw == 0 ? 168 : raw, 1, 24 * 30);
}

}

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

json Stats::vaultSync(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view sync stats for this vault.");

    const auto stats = vh::db::query::sync::Stats::getVaultSyncHealth(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultActivity(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view activity stats for this vault.");

    const auto stats = vh::db::query::vault::Activity::getVaultActivity(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultShares(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view share stats for this vault.");

    const auto stats = vh::db::query::share::Stats::getVaultShareStats(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultRecovery(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view recovery stats for this vault.");

    const auto stats = vh::db::query::vault::Recovery::getVaultRecovery(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultOperations(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view operation stats for this vault.");

    const auto stats = vh::db::query::stats::OperationStats::snapshotForVault(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultStorage(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view storage backend stats for this vault.");

    return {{"stats", vh::stats::model::StorageBackendStats::snapshotForVault(vaultId)}};
}

json Stats::vaultRetention(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view retention stats for this vault.");

    const auto stats = vh::db::query::stats::RetentionStats::snapshotForVault(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultTrends(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view trend stats for this vault.");

    const auto stats = vh::db::query::stats::Snapshot::vaultTrends(vaultId, statsTrendWindowHours(payload));
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::vaultSecurity(const json& payload, const std::shared_ptr<Session>& session) {
    const auto& vaultId = payload.at("vault_id").get<uint32_t>();

    using Perm = permission::admin::VaultPermissions;

    if (!resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = { Perm::View, Perm::ViewStats },
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to view security stats for this vault.");

    const auto stats = vh::db::query::vault::Security::getVaultSecurity(vaultId);
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::dashboardOverview(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view dashboard overview stats.");
    return {{"stats", vh::stats::model::DashboardOverview::snapshot(vh::stats::model::dashboardOverviewRequestFromJson(payload))}};
}

json Stats::systemHealth(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view system health.");
    return {{"stats", vh::stats::model::SystemHealth::snapshot()}};
}

json Stats::systemThreadPools(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view thread pool stats.");
    return {{"stats", vh::stats::model::ThreadPoolManagerSnapshot::snapshot()}};
}

json Stats::systemFuse(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view FUSE stats.");
    const auto& stats = runtime::Deps::get().fuseStats;
    if (!stats) throw std::runtime_error("No FUSE stats available.");
    return {{"stats", stats->snapshot()}};
}

json Stats::systemDb(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view database health.");
    const auto stats = vh::db::query::stats::DbStats::snapshot();
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::systemOperations(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view operation queue stats.");
    const auto stats = vh::db::query::stats::OperationStats::snapshot();
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::systemConnections(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view connection stats.");
    return {{"stats", vh::stats::model::ConnectionStats::snapshot()}};
}

json Stats::systemStorage(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view storage backend stats.");
    return {{"stats", vh::stats::model::StorageBackendStats::snapshot()}};
}

json Stats::systemRetention(const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view retention stats.");
    const auto stats = vh::db::query::stats::RetentionStats::snapshot();
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
}

json Stats::systemTrends(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->isAdmin()) throw std::runtime_error("Must be an admin to view trend stats.");
    const auto stats = vh::db::query::stats::Snapshot::systemTrends(statsTrendWindowHours(payload));
    return {{"stats", stats ? json(*stats) : json(nullptr)}};
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
