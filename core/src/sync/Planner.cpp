#include "sync/Planner.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/Cloud.hpp"
#include "sync/model/helpers.hpp"
#include "sync/model/Conflict.hpp"
#include "fs/model/File.hpp"

using namespace vh::sync;
using namespace vh::sync::model;
using namespace vh::fs::model;

std::vector<Action> Planner::build(
    const std::shared_ptr<Cloud>& ctx,
    const std::shared_ptr<RemotePolicy>& policy)
{
    std::vector<Action> plan;

    if (policy->wantsEnsureDirectories()) {
        plan.push_back({ ActionType::EnsureDirectories, {.rel = u8""}, nullptr, nullptr });
    }

    const auto keys = ctx->allKeysSorted();
    plan.reserve(keys.size() + 8);

    for (const auto& k : keys) {
        std::shared_ptr<File> L;
        std::shared_ptr<File> R;

        if (auto it = ctx->localMap.find(k.rel); it != ctx->localMap.end()) L = it->second;
        if (auto it = ctx->s3Map.find(k.rel);    it != ctx->s3Map.end())    R = it->second;

        if (L && !R) {
            if (policy->uploadLocalOnly())
                plan.push_back({ ActionType::Upload, k, L, nullptr });
            continue;
        }

        if (!L && R) {
            if (policy->downloadRemoteOnly())
                plan.push_back({ ActionType::Download, k, nullptr, R });
            continue;
        }

        if (L && R) {
            // Fast-path skip if equal content (let ctx decide via hashes, remoteHashMap, etc.)
            if (*L == *R) continue;

            // Conflict check lives in ctx (since it needs hashes/mtimes/last_success_at/etc.)
            if (auto c = ctx->maybeBuildConflict(L, R)) {
                if (ctx->handleConflict(c)) continue; // Ask mode, no action planned

                // auto-resolved => planner needs to translate resolution into an action
                switch (c->resolution) {
                case Conflict::Resolution::KEPT_LOCAL:
                    plan.push_back({ ActionType::Upload, k, L, R });
                    break;
                case Conflict::Resolution::KEPT_REMOTE:
                    plan.push_back({ ActionType::Download, k, L, R });
                    break;
                // TODO: finishing handling Resolution cases (e.g. KEPT_BOTH, etc.)
                default:
                    break;
                }
                continue;
            }

            // Non-conflict decision: by strategy/policy
            if (auto a = policy->decideForBoth(L, R))
                plan.push_back({ *a, k, L, R });
        }
    }

    if (policy->deleteRemoteLeftovers())
        for (auto& [rel, r] : ctx->s3Map)
            if (!ctx->localMap.contains(rel))
                plan.push_back({ ActionType::DeleteRemote, {rel}, nullptr, r });

    if (policy->deleteLocalLeftovers())
        for (auto& [rel, l] : ctx->localMap)
            if (!ctx->s3Map.contains(rel))
                plan.push_back({ ActionType::DeleteLocal, {rel}, l, nullptr });

    policy->preflightSpaceForPlan(ctx, plan);
    return plan;
}
