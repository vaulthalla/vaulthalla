#include "sync/model/RemotePolicy.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/interval.hpp"
#include "sync/model/Conflict.hpp"
#include "fs/model/File.hpp"
#include "sync/Cloud.hpp"
#include "storage/Engine.hpp"
#include "sync/model/Event.hpp"

#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using namespace vh::sync::model;
using namespace vh::fs::model;
using namespace vh::concurrency;
using namespace vh::db::encoding;

RemotePolicy::RemotePolicy(const pqxx::row& row)
    : Policy(row),
      strategy(strategyFromString(row.at("strategy").as<std::string>())),
      conflict_policy(rsConflictPolicyFromString(row.at("conflict_policy").as<std::string>())) {
    rehash_config();
}

void RemotePolicy::rehash_config() {
    config_hash = "vault_id=" + std::to_string(vault_id) +
                  ";interval=" + std::to_string(interval.count()) +
                  ";enabled=" + (enabled ? "true" : "false") +
                  ";strategy=" + to_string(strategy) +
                  ";conflict_policy=" + to_string(conflict_policy);
}

bool RemotePolicy::resolve_conflict(const std::shared_ptr<Conflict>& conflict) const {
    if (conflict_policy == ConflictPolicy::Ask) return false;

    if (conflict_policy == ConflictPolicy::KeepLocal) conflict->resolution = Conflict::Resolution::KEPT_LOCAL;
    else if (conflict_policy == ConflictPolicy::KeepRemote) conflict->resolution = Conflict::Resolution::KEPT_REMOTE;
    else if (conflict_policy == ConflictPolicy::KeepNewest) {
        if (conflict->artifacts.local.file->updated_at == std::time_t{0} || conflict->artifacts.upstream.file->updated_at == std::time_t{0})
            return false; // If either timestamp is missing, we can't determine which is newer, so we ask the user.

        if (conflict->artifacts.local.file->updated_at > conflict->artifacts.upstream.file->updated_at) conflict->resolution = Conflict::Resolution::KEPT_LOCAL;
        else conflict->resolution = Conflict::Resolution::KEPT_REMOTE;
    }

    return true;
}

bool RemotePolicy::wantsEnsureDirectories() const {
    if (strategy == Strategy::Mirror) return false;
    return true;
}

bool RemotePolicy::downloadRemoteOnly() const {
    switch (strategy) {
    case Strategy::Cache:
        // Cache pulls remote content as-needed to satisfy cache state
        return true;
    case Strategy::Sync:
        // Sync pulls remote-only files to make local complete
        return true;
    case Strategy::Mirror:
        // Mirror depends on direction.
        // KeepRemote mirror should pull remote-only.
        // KeepLocal mirror does NOT pull remote-only in your old behavior (it deletes them remotely).
        // KeepNewest is ambiguous; safest is "pull remote-only" = true? but that can surprise.
        switch (conflict_policy) {
        case ConflictPolicy::KeepRemote: return true;
        case ConflictPolicy::KeepLocal:  return false;
        case ConflictPolicy::KeepNewest: return true; // reasonable default: preserve data by downloading
        case ConflictPolicy::Ask:        return true; // preserve, user can delete later
        }
    }
    return false;
}

bool RemotePolicy::uploadLocalOnly() const {
    switch (strategy) {
    case Strategy::Cache:
        // Cache treats local as authoritative for what's present locally
        // (your old CacheSyncTask uploads local-only)
        return true;
    case Strategy::Sync:
        // Sync uploads local-only to remote
        return true;
    case Strategy::Mirror:
        // Mirror depends on direction:
        // KeepLocal: push local-only to remote to match local image.
        // KeepRemote: do NOT upload local-only; it'll be deleted locally.
        // KeepNewest/Ask: safest is upload local-only? I'd say NO for KeepNewest (ambiguous),
        // YES for Ask? I'd avoid surprises: don't upload unless explicitly KeepLocal.
        switch (conflict_policy) {
        case ConflictPolicy::KeepLocal:  return true;
        case ConflictPolicy::KeepRemote: return false;
        case ConflictPolicy::KeepNewest: return false;
        case ConflictPolicy::Ask:        return false;
        }
    }
    return false;
}

bool RemotePolicy::deleteRemoteLeftovers() const {
    if (strategy != Strategy::Mirror) return false;

    // Old MirrorKeepLocal deleted remote leftovers (remote files not present locally)
    // MirrorKeepRemote did not delete remote leftovers.
    switch (conflict_policy) {
    case ConflictPolicy::KeepLocal:  return true;
    case ConflictPolicy::KeepRemote: return false;
    case ConflictPolicy::KeepNewest: return false; // safest default
    case ConflictPolicy::Ask:        return false;
    }
    return false;
}

bool RemotePolicy::deleteLocalLeftovers() const {
    if (strategy != Strategy::Mirror) return false;

    // Old MirrorKeepRemote deleted local leftovers
    switch (conflict_policy) {
    case ConflictPolicy::KeepRemote: return true;
    case ConflictPolicy::KeepLocal:  return false;
    case ConflictPolicy::KeepNewest: return false; // safest default
    case ConflictPolicy::Ask:        return false;
    }
    return false;
}

// NOTE: This method should ONLY decide action type for entries where BOTH local and remote exist.
// It should NOT do hashing or conflict construction; keep it pure.
// It assumes the planner already knows:
// - whether content is equal (fast-path) OR
// - whether localNewer/remoteNewer (mtime compare)
//
// If you want this function to incorporate mtime compare, LocalInfo/RemoteInfo need those fields accessible.
// I’m assuming LocalInfo/RemoteInfo expose file->updated_at etc.
std::optional<ActionType> RemotePolicy::decideForBoth(const std::shared_ptr<File>& L, const std::shared_ptr<File>& R) const {
    // If content is equal, planner should skip before calling this.
    // We'll still be defensive if you pipe equality info in later.
    const auto lts = L->updated_at;
    const auto rts = R->updated_at;

    switch (strategy) {
    case Strategy::Cache:
        // Cache downloads if remote is newer; never uploads local-newer
        if (lts == std::time_t{0} || rts == std::time_t{0}) {
            // Without timestamps, safest cache behavior is "do nothing" (avoid overwrite)
            return std::nullopt;
        }
        if (rts > lts) return ActionType::Download;
        return std::nullopt;

    case Strategy::Sync:
        // Two-way sync: newest wins by mtime unless conflict system intervenes elsewhere
        if (lts == std::time_t{0} || rts == std::time_t{0}) {
            // Without timestamps, avoid destructive overwrite; prefer download to preserve remote?
            // For "sync" I'd pick download (remote source of truth) to avoid pushing bad local.
            return ActionType::Download;
        }
        if (lts > rts) return ActionType::Upload;
        if (rts > lts) return ActionType::Download;
        return std::nullopt;

    case Strategy::Mirror:
        // Mirror is explicit direction unless KeepNewest
        switch (conflict_policy) {
        case ConflictPolicy::KeepLocal:
            return ActionType::Upload;
        case ConflictPolicy::KeepRemote:
            return ActionType::Download;
        case ConflictPolicy::KeepNewest:
            if (lts == std::time_t{0} || rts == std::time_t{0}) return std::nullopt;
            if (lts > rts) return ActionType::Upload;
            if (rts > lts) return ActionType::Download;
            return std::nullopt;
        case ConflictPolicy::Ask:
            // planner should create Conflict and bail earlier; but we’ll no-op here too
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void RemotePolicy::preflightSpaceForPlan(const std::weak_ptr<Cloud>& ctx, const std::vector<Action>& plan) const {
    auto task = ctx.lock();
    if (!task) return; // nothing we can enforce

    // Gather planned downloads
    std::vector<std::shared_ptr<File>> downloads;
    downloads.reserve(plan.size());

    for (const auto& a : plan) {
        if (a.type != ActionType::Download) continue;

        // Prefer remote artifact when present; fall back to local if your planner uses that.
        if (a.remote) downloads.push_back(a.remote);
        else if (a.local) downloads.push_back(a.local);
    }

    if (downloads.empty()) return;

    // Decide enforcement strictness by strategy
    // - Sync: hard requirement (Safe behavior)
    // - Cache: you probably want eviction-based behavior; until that's centralized, you can choose:
    //          (a) hard gate (deterministic) OR
    //          (b) allow and let cache layer evict during execution
    // - Mirror: if downloads are planned (KeepRemote/KeepNewest), it should still gate.
    const bool hardGate =
        (strategy == Strategy::Sync) ||
        (strategy == Strategy::Mirror) ||
        (strategy == Strategy::Cache /* set false later if you centralize eviction */);

    if (!hardGate) return;

    // Compute required bytes. Prefer your existing helper if it exists.
    uintmax_t required = 0;

    // If SyncTask exposes computeReqFreeSpaceForDownload(downloads), use it.
    // (This was in your Safe/Cache tasks originally.)
    if constexpr (requires { Cloud::computeReqFreeSpaceForDownload(downloads); }) {
        required = Cloud::computeReqFreeSpaceForDownload(downloads);
    } else {
        // Fallback: sum sizes. Less accurate (doesn't include temp overhead),
        // but deterministic and safe enough as a baseline.
        for (const auto& f : downloads) {
            if (!f) continue;
            required += f->size_bytes;
        }
    }

    const auto available = task->engine->freeSpace();

    if (available >= required) return;

    // Decorate event for observability (if present)
    if (task->event) {
        task->event->error_code = "Insufficient Disk Space";
        std::ostringstream oss;
        oss << "Not enough free space for planned downloads. Required: "
            << required << ", Available: " << available;
        task->event->error_message = oss.str();
        task->event->stall_reason = task->event->error_code;
    }

    throw std::runtime_error(
        "[RemotePolicy::preflightSpaceForPlan] Not enough free space. Required: " +
        std::to_string(required) + ", Available: " + std::to_string(available));
}

void vh::sync::model::to_json(nlohmann::json& j, const RemotePolicy& s) {
    to_json(j, static_cast<const Policy&>(s));
    j["strategy"] = to_string(s.strategy);
    j["conflict_policy"] = to_string(s.conflict_policy);
}

void vh::sync::model::from_json(const nlohmann::json& j, RemotePolicy& s) {
    from_json(j, static_cast<Policy&>(s));
    s.strategy = strategyFromString(j.at("strategy").get<std::string>());
    s.conflict_policy = rsConflictPolicyFromString(j.at("conflict_policy").get<std::string>());
}

std::string vh::sync::model::to_string(const RemotePolicy::Strategy& s) {
    switch (s) {
    case RemotePolicy::Strategy::Cache: return "cache";
    case RemotePolicy::Strategy::Sync: return "sync";
    case RemotePolicy::Strategy::Mirror: return "mirror";
    default: throw std::invalid_argument("Unknown sync strategy");
    }
}

std::string vh::sync::model::to_string(const RemotePolicy::ConflictPolicy& cp) {
    switch (cp) {
    case RemotePolicy::ConflictPolicy::KeepLocal: return "keep_local";
    case RemotePolicy::ConflictPolicy::KeepRemote: return "keep_remote";
    case RemotePolicy::ConflictPolicy::KeepNewest: return "keep_newest";
    case RemotePolicy::ConflictPolicy::Ask: return "ask";
    default: throw std::invalid_argument("Unknown conflict policy");
    }
}

RemotePolicy::Strategy vh::sync::model::strategyFromString(const std::string& str) {
    if (str == "cache") return RemotePolicy::Strategy::Cache;
    if (str == "sync") return RemotePolicy::Strategy::Sync;
    if (str == "mirror") return RemotePolicy::Strategy::Mirror;
    throw std::invalid_argument("Unknown sync strategy: " + str);
}

RemotePolicy::ConflictPolicy vh::sync::model::rsConflictPolicyFromString(const std::string& str) {
    if (str == "keep_local") return RemotePolicy::ConflictPolicy::KeepLocal;
    if (str == "keep_remote") return RemotePolicy::ConflictPolicy::KeepRemote;
    if (str == "keep_newest") return RemotePolicy::ConflictPolicy::KeepNewest;
    if (str == "ask") return RemotePolicy::ConflictPolicy::Ask;
    throw std::invalid_argument("Unknown conflict policy: " + str);
}

std::string vh::sync::model::to_string(const std::shared_ptr<RemotePolicy>& sync) {
    if (!sync) return "null";
    return "Remote Vault Sync Configuration:\n"
           "  Vault ID: " + std::to_string(sync->vault_id) + "\n"
           "  Interval: " + intervalToString(sync->interval) + "\n"
           "  Enabled: " + (sync->enabled ? "true" : "false") + "\n"
           "  Strategy: " + to_string(sync->strategy) + "\n"
           "  Conflict Policy: " + to_string(sync->conflict_policy) + "\n"
           "  Last Sync At: " + timestampToString(sync->last_sync_at) + "\n"
           "  Last Success At: " + timestampToString(sync->last_success_at) + "\n"
           "  Created At: " + timestampToString(sync->created_at) + "\n"
           "  Updated At: " + timestampToString(sync->updated_at);
}
