#include "sync/Executor.hpp"
#include "sync/Cloud.hpp"
#include "sync/model/Action.hpp"

using namespace vh::sync;

void Executor::run(const std::shared_ptr<Cloud>& ctx, const std::vector<model::Action>& plan) {
    // Option: stable grouping for determinism & perf
    // EnsureDirectories first, then uploads, then downloads, then deletes
    for (const auto type : {model::ActionType::EnsureDirectories, model::ActionType::Upload, model::ActionType::Download,
                      model::ActionType::DeleteRemote, model::ActionType::DeleteLocal}) {
        for (const auto& a : plan) {
            if (a.type != type) continue;
            dispatch(ctx, a);
        }
        ctx->processFutures(); // barrier between phases
        }
}

void Executor::dispatch(const std::shared_ptr<Cloud>& ctx, const model::Action& action) {
    switch (action.type) {
    case model::ActionType::EnsureDirectories:
        ctx->ensureDirectoriesFromRemote(); // extracted shared function
        return;
    case model::ActionType::Upload:
        ctx->upload(action.local);
        return;
    case model::ActionType::Download:
        // if cache mode sets freeAfterDownload, forward it
        ctx->download(action.remote ? action.remote : action.local, action.freeAfterDownload);
        return;
    case model::ActionType::DeleteLocal:
        ctx->remove(action.local, tasks::Delete::Type::LOCAL);
        return;
    case model::ActionType::DeleteRemote:
        ctx->remove(action.remote, tasks::Delete::Type::REMOTE);
        return;
    }
}

